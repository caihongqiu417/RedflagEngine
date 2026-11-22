// 将 tevy 提供的米制建筑、石堆与植物导入维修站，保留原模型、材质和纹理。
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const cache = path.resolve(process.argv[2] || 'tmp/salvage-source');
const output = path.join(root, 'RedflagForge/assets/game/station/salvage-kit');
const require = createRequire(path.join(cache, 'package.json'));
const { NodeIO } = require('@gltf-transform/core');
const { ALL_EXTENSIONS } = require('@gltf-transform/extensions');
const { dequantize, getBounds } = require('@gltf-transform/functions');
const { MeshoptDecoder } = require('meshoptimizer');
const basis = process.argv[3] || path.join(cache, 'node_modules/basisu/bin/win/x64/basisu.exe');

const models = [
    ['ScrapShanty', 'scrap-shanty/v1/lod0', 'be31326d86981fc7b0297122179dea28efea19c442ac4910981209e3db72d95a'],
    ['Warehouse', 'warehouse/v2/lod1', '989912e784a1fc1a822df7ddc4e3b65be53149744cc40a6d66a7e548d9760fc4'],
    ['Watchtower', 'watchtower-scrap/v1/lod1', 'bd7330bb914573022d5c908fbee267ba6a66dfc0bc969e24715ab3fb4ddebf6f'],
    ['LoadingBay', 'scaffolding-loading-bay/v1/lod0', '82b7320be67952c7c309ecabfab648b9dce02b337f08a62526a9990a73860fe3'],
    ['Rubble', 'rubble-heap-small/v1/lod0', '5a4cb1182ba6086d09c885201aeb9fe986b632b25f1186185eda23668dbab479'],
    ['DryGrass', 'savanna-grass-patch/v1/lod2', 'efb38391b11d6bc973176282d370c335f2356658f4e59616f14d551f5d43c8e2'],
    ['Thornbush', 'savanna-thornbush/v1/lod1', '708954adc0ccef357cd70a97a68dd5dbebed765d25596d1ee81dbc7e8ca5ce0c']
];
const checksum = data => createHash('sha256').update(data).digest('hex');
await MeshoptDecoder.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS)
    .registerDependencies({ 'meshopt.decoder': MeshoptDecoder });

async function importModel(name, version, expected) {
    const source = 'https://tevy.dev/models/' + version + '.glb';
    const response = await fetch(source);
    if (!response.ok) throw new Error(source + ' ' + response.status);
    const bytes = Buffer.from(await response.arrayBuffer());
    if (checksum(bytes) !== expected) throw new Error('下载的模型与固定版本不符 ' + source);

    const doc = await io.readBinary(bytes);
    const scene = doc.getRoot().listScenes()[0];
    await doc.transform(dequantize());

    // 在独立目录解出最高分辨率的 KTX2 图层。RGBA 同时保留草叶等材质的透明度。
    const scratch = path.join(cache, name);
    await mkdir(scratch, { recursive: true });
    for (const [index, texture] of doc.getRoot().listTextures().entries()) {
        if (texture.getMimeType() !== 'image/ktx2') continue;
        const input = path.join(scratch, 'map' + index + '.ktx2');
        await writeFile(input, texture.getImage());
        execFileSync(basis, ['-unpack', '-no_ktx', '-format_only', '6', '-file', input], { cwd: scratch });
        const pixels = await readFile(path.join(scratch, 'map' + index + '_unpacked_rgba_BC7_RGBA_0_0000.png'));
        texture.setImage(pixels).setMimeType('image/png');
    }
    for (const extension of doc.getRoot().listExtensionsUsed()) {
        if (['EXT_meshopt_compression', 'KHR_texture_basisu'].includes(extension.extensionName)) extension.dispose();
    }

    if (name === 'ScrapShanty') {
        const door = doc.getRoot().listNodes().find(node => node.getName() === 'door');
        const angle = 58 * Math.PI / 180;
        door.setRotation([0, Math.sin(angle / 2), 0, Math.cos(angle / 2)]);
    }

    // 每件资源以地面为零。保留作者给根节点的平移和旋转，包括有多个根的场景。
    const base = getBounds(scene).min[1];
    for (const child of scene.listChildren()) {
        const [x,y,z] = child.getTranslation();
        child.setTranslation([x, y - base, z]);
    }
    const glb = await io.writeBinary(doc);
    await writeFile(path.join(output, name + '.glb'), glb);
    console.log(name, glb.length);
    return { asset: name, source, license: 'CC0-1.0', originalSha256: expected,
        sha256: checksum(glb), bytes: glb.length, bounds: getBounds(scene) };
}

await mkdir(output, { recursive: true });
const records = [];
for (const model of models) records.push(await importModel(...model));
await writeFile(path.join(output, 'sources.json'), JSON.stringify(records, null, 2) + '\n');
