"""Run inside Blender's background process, called by fetch_sun_temple.py."""
import json
from pathlib import Path
import sys

import bpy


source, target = map(Path, sys.argv[sys.argv.index("--") + 1:])
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.fbx_import(filepath=str(source))

# FBX can give several rock materials the same name. Read their actual image links.
materials = {}
for material in bpy.data.materials:
    files = {}
    for node in list(material.node_tree.nodes):
        if node.type == "TEX_IMAGE":
            image = Path(node.image.filepath)
            files[image.stem.rsplit("_", 1)[1]] = image.with_suffix(".png").name
            material.node_tree.nodes.remove(node)
    materials[material.name] = files

bpy.ops.object.select_all(action="DESELECT")
for obj in bpy.context.scene.objects:
    obj.select_set(obj.type == "MESH")
# The building is static. Bake the 517 placements into one mesh so a shared
# pillar material is loaded once, even when it occurs in many places.
building = bpy.data.objects.new("Sun Temple", bpy.data.meshes.new("Sun Temple"))
bpy.context.collection.objects.link(building)
building.select_set(True)
bpy.context.view_layer.objects.active = building
bpy.ops.object.join()
bpy.ops.export_scene.gltf(filepath=str(target), export_format="GLTF_SEPARATE", use_selection=True,
                          export_animations=False, export_tangents=True)

# ORCA stores occlusion, roughness and metalness together in R, G and B.
model = json.loads(target.read_text(encoding="utf-8"))
model["images"] = []
model["textures"] = []
indices = {}


def texture(name):
    if name not in indices:
        indices[name] = len(model["images"])
        model["images"].append({"uri": "Textures/" + name})
        model["textures"].append({"source": indices[name]})
    return {"index": indices[name]}


for material in model["materials"]:
    name = material["name"]
    files = materials[name]
    material.clear()
    material.update(name=name, pbrMetallicRoughness={
        "baseColorTexture": texture(files["BaseColor"]),
        "metallicRoughnessTexture": texture(files["Specular"]),
    }, normalTexture=texture(files["Normal"]), occlusionTexture=texture(files["Specular"]))
    if "Emissive" in files:
        material.update(emissiveTexture=texture(files["Emissive"]), emissiveFactor=[1, 1, 1])
    if name.endswith(".DoubleSided"):
        material.update(doubleSided=True, alphaMode="MASK", alphaCutoff=0.5)
target.write_text(json.dumps(model, separators=(",", ":")), encoding="utf-8")
