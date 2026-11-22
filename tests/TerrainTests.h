#pragma once

#include "Redflag/Terrain/TerrainComponent.h"
#include "Redflag/Scene/Entity.h"

inline void CheckTerrain()
{
    using namespace Redflag;
    const std::string path = "../tests/fixtures/terrain/height16.png";
    const Ref<Terrain> grid = Terrain::Load(path);
    Check(grid && grid->GetWidth() == 5 && grid->GetDepth() == 4, "rectangular 16-bit height image");
    if (!grid) return;
    const Ref<Terrain> low = Terrain::Load("../tests/fixtures/terrain/height8.png");
    Check(low && low->GetHeight(1,0) == 85*257 && low->GetHeight(0,1) == 170*257, "8-bit samples, original top row");
    Check(grid->GetHeight(1,1) == 40000 && grid->GetHeight(0,3) == 65535, "high unsigned samples preserve precision and orientation");
    Check(!grid->Sample(-.001f,0,1,3) && !grid->Sample(1,3.01f,1,3) && grid->Sample(4,3,1,3),
        "inclusive border with no height beyond the mesh");

    const auto chunks = grid->BuildChunks(1, 3, 2);
    Check(chunks.size() == 4, "four chunks, last row has one cell");
    std::map<std::pair<int,int>, MeshVertex> shared;
    uint32_t triangles = 0;
    bool seams = true, up = true;
    for (const TerrainChunk& chunk : chunks)
    {
        for (const MeshVertex& vertex : chunk.Vertices)
        {
            const auto key = std::make_pair(int(vertex.Position.x), int(vertex.Position.z));
            auto found = shared.emplace(key, vertex);
            const MeshVertex& seen = found.first->second;
            seams &= seen.Position == vertex.Position && seen.Normal == vertex.Normal && seen.TexCoord == vertex.TexCoord;
        }
        for (size_t i=0; i<chunk.Indices.size(); i+=3)
        {
            const glm::vec3 a = chunk.Vertices[chunk.Indices[i]].Position;
            const glm::vec3 b = chunk.Vertices[chunk.Indices[i+1]].Position;
            const glm::vec3 c = chunk.Vertices[chunk.Indices[i+2]].Position;
            const glm::vec3 midpoint = (a+b+c)/3.f;
            const auto sample = grid->SampleSurface(midpoint.x,midpoint.z,1,3);
            const glm::vec3 normal = glm::normalize(glm::cross(b-a,c-a));
            up &= sample && sample->Normal.y > 0 && std::abs(sample->Height-midpoint.y) < .00001f &&
                glm::distance(sample->Normal, normal) < .00001f;
            ++triangles;
        }
    }
    Check(seams && shared.size()==20 && triangles==24, "all 12 cells, matching border positions, normals and UVs");
    Check(up, "CPU samples agree with both mesh triangles and their normals");

    Ref<Scene> edit = CreateRef<Scene>();
    Entity ground = edit->CreateEntityWithUUID(21,"Ground");
    TerrainComponent& field = ground.AddComponent<TerrainComponent>();
    field.Heightmap = path;
    field.CellSize = 1;
    field.HeightScale = 3;
    Ref<Scene> game = Scene::Copy(edit);
    Entity playing = game->GetEntityByUUID(21);
    game->OnSimulationStart();
    bool hits = true;
    for (const auto& item : chunks)
        for (size_t i=0; i<item.Indices.size(); i+=3)
        {
            glm::vec3 point(0.f);
            for (size_t j=0;j<3;++j) point += item.Vertices[item.Indices[i+j]].Position / 3.f;
            const auto hit = game->Raycast(point+glm::vec3(0,5,0), {0,-1,0}, 8);
            const auto surface = grid->SampleSurface(point.x,point.z,1,3);
            hits &= hit && hit->Entity == 21 && glm::distance(point,hit->Position)<.0001f && glm::dot(hit->Normal,surface->Normal)>.99999f;
        }
    Check(hits,"PhysX hit positions and normals match every rendered triangle");

    // 地形能支撑箱子。在播放的场景里关闭碰撞后，原来的地面随即从射线和模拟中消失。
    Entity box = game->CreateEntityWithUUID(22,"Box");
    box.GetComponent<TransformComponent>().Translation = {3.5f,4.f,1.5f};
    box.AddComponent<RigidBody3DComponent>().Type = BodyType3D::Dynamic;
    box.AddComponent<BoxCollider3DComponent>().HalfSize = glm::vec3(.2f);
    for (int i=0;i<180;++i) game->OnUpdatePhysics(1.f/60);
    Check(std::abs(box.GetComponent<TransformComponent>().Translation.y-1.7f)<.05f, "dynamic box rests on heightfield");
    TerrainComponent& instance = playing.GetComponent<TerrainComponent>();
    instance.Collision = false;
    for (int i=0;i<60;++i) game->OnUpdatePhysics(1.f/60);
    Check(!game->Raycast({1,5,1},{0,-1,0},10) && box.GetComponent<TransformComponent>().Translation.y<0 && field.Collision,
        "disabling runtime terrain leaves the editor scene intact and box falls");

    instance.Collision = true;
    auto& transform = playing.GetComponent<TransformComponent>();
    transform.Translation = {11,2,3};
    transform.Rotation.y = glm::radians(90.f);
    transform.Scale = {2,3,.5f};
    game->OnUpdatePhysics(1.f/60);
    const glm::vec3 local(.75f,*grid->Sample(.75f,.25f,1,3),.25f);
    const glm::vec3 moved(transform.GetTransform()*glm::vec4(local,1));
    const auto hit = game->Raycast(moved+glm::vec3(0,5,0),{0,-1,0},7);
    Check(hit && hit->Entity == 21 && glm::distance(hit->Position,moved)<.001f, "translated, yawed and scaled terrain");
    playing.RemoveComponent<TerrainComponent>();
    Check(!game->Raycast(moved+glm::vec3(0,5,0),{0,-1,0},7),"removing component immediately removes collision");
    game->OnSimulationStop();
}

inline void CheckQuarryTerrain()
{
    using namespace Redflag;
    const Ref<Terrain> heights = Terrain::Load("assets/terrain/quarry/quarry-height.png");
    Check(heights && heights->GetWidth()==129 && heights->GetDepth()==129, "read 129 by 129 USGS quarry heights");
    if (!heights) return;
    // 来自同目录 sources.json 的像素间距与海拔范围。测试在局部坐标中以最低点为零。
    const float spacing = 7.2626f, relief = 2107.67578125f - 1412.65625f;
    const auto chunks = heights->BuildChunks(spacing,relief,32);
    size_t indices = 0;
    for (const TerrainChunk& chunk : chunks) indices += chunk.Indices.size();
    Check(chunks.size()==16 && indices/3==32768, "quarry covers 16 full chunks");

    PhysicsWorld world;
    world.AddTerrain(40,*heights,{0,0,0},glm::quat(1,0,0,0),{1,1,1},spacing,relief);
    float error = 0.f;
    bool hits = true;
    for (int z=0; z<128; z+=16)
        for (int x=0; x<128; x+=16)
        {
            const float px=(x+.75f)*spacing, pz=(z+.25f)*spacing;
            const auto sample=heights->SampleSurface(px,pz,spacing,relief);
            const auto hit=world.Raycast({px, relief+10, pz}, {0,-1,0}, relief+11);
            hits &= hit && sample && hit->Entity==40 && glm::dot(hit->Normal,sample->Normal)>.9999f;
            if (hit && sample) error=std::max(error, std::abs(hit->Position.y-sample->Height));
        }
    Check(hits && error < .001f, "real quarry: 64 PhysX rays agree within one millimetre");
    std::cout << "Quarry maximum height difference = " << error << " m\n";
}

inline void ResaveTerrain(const char* source, const char* destination)
{
    using namespace Redflag;
    Ref<Scene> scene = CreateRef<Scene>();
    const bool loaded = SceneSerializer(scene).Deserialize(source);
    Check(loaded, "read packaged terrain entity");
    if (!loaded) return;

    auto fields = scene->GetRegistry().view<TerrainComponent>();
    Check(fields.begin() != fields.end(), "terrain component present");
    for (entt::entity entity : fields)
    {
        const TerrainComponent& field = fields.get<TerrainComponent>(entity);
        Check(field.GetTerrain() && std::filesystem::is_regular_file(field.BaseColorTexture),
            "height image and surface texture resolve inside the unpacked game");
    }
    SceneSerializer(scene).Serialize(destination);
}
