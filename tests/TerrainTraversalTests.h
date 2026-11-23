#pragma once

#include "Redflag/Gameplay/TerrainTraversal.h"
#include "TerrainFootTests.h"

inline void CheckTraversal()
{
    using namespace Redflag;
    Ref<Scene> scene = CreateRef<Scene>();
    auto box = [&](uint64_t id, glm::vec3 p, glm::vec3 size) {
        Entity object = scene->CreateEntityWithUUID(id,"Platform");
        object.GetComponent<TransformComponent>().Translation = p;
        object.AddComponent<RigidBody3DComponent>();
        object.AddComponent<BoxCollider3DComponent>().HalfSize = size * .5f;
        return object;
    };
    // 六条互不遮挡的路线分别放台阶、跳台、攀爬平台、过高的墙、缺口和低梁。
    for (int lane=0;lane<6;lane++) {
        float z = lane * 4.f;
        box(20+lane,{-2.f,-.25f,z},{4,.5f,2});
        if (lane!=4) {
            const float heights[6]={.20f,.72f,1.42f,2.2f,0.f,.76f};
            float h=heights[lane];
            box(40+lane,{1.5f,h*.5f,z},{3,h,2});
        } else box(44,{2.45f,-.25f,z},{3,.5f,2});
    }
    box(62,{-.4f,2.13f,20.f},{1.4f,.25f,2});
    Entity ground=scene->CreateEntityWithUUID(80,"Heightfield");
    ground.GetComponent<TransformComponent>().Translation={8,0,-4};
    TerrainComponent& heightmap=ground.AddComponent<TerrainComponent>();
    heightmap.Heightmap="../tests/fixtures/terrain/height16.png";
    heightmap.HeightScale=.75f;
    Entity actor=scene->CreateEntityWithUUID(1,"Walker");
    auto& position=actor.GetComponent<TransformComponent>().Translation;
    scene->OnSimulationStart();
    auto reset=[&](int lane) {
        scene->GetRegistry().remove<TerrainTraversal>(static_cast<entt::entity>(actor));
        position={-.7f,.07f,4.f*lane};
    };
    auto run=[&](int lane, TraversalMode mode, const char* name) {
        reset(lane);
        bool observed=false;
        for(int i=0;i<120;i++) {
            const auto state=MoveCharacter(*scene,1,{.025f,0,0},.28f,1.f/60,TraversalInput::Automatic);
            observed |= state==mode;
            if (state==TraversalMode::Blocked || position.x>.8f) {
                if (!scene->GetRegistry().get<TerrainTraversal>(static_cast<entt::entity>(actor)).InAir()) break;
            }
        }
        std::cout<<name<<" x="<<position.x<<" y="<<position.y<<"\n";
        Check(observed && (mode==TraversalMode::Blocked ? position.x<0 : position.x>.7f), name);
    };
    run(0,TraversalMode::Step,"step onto a 20 cm platform");
    run(1,TraversalMode::Jump,"jump onto a 72 cm platform");
    run(2,TraversalMode::Climb,"climb a 1.42 m ledge");
    run(3,TraversalMode::Blocked,"reject a 2.20 m wall");
    run(4,TraversalMode::Jump,"jump across a 95 cm gap");
    run(5,TraversalMode::Blocked,"reject a jump under the low overhead beam");

    reset(1);
    for(int i=0;i<60;++i)
        if (MoveCharacter(*scene,1,{.035f,0,0},.28f,1.f/60,TraversalInput::Automatic)==TraversalMode::Jump) break;
    scene->GetEntityByUUID(41).RemoveComponent<BoxCollider3DComponent>();
    Check(MoveCharacter(*scene,1,{0,0,0},.28f,1.f/60)==TraversalMode::Blocked && position.x<0,
        "removing the landing platform during a jump cancels the crossing");

    reset(2);
    for(int i=0;i<45;++i) {
        if (MoveCharacter(*scene,1,{.035f,0,0},.28f,1.f/60,TraversalInput::Automatic)==TraversalMode::Climb) break;
    }
    glm::vec3 takeoff=scene->GetRegistry().get<TerrainTraversal>(static_cast<entt::entity>(actor)).Start;
    CancelTraversal(*scene,1);
    Check(glm::distance(takeoff,position)<.005f,"cancel the climb and stand on the takeoff surface");

    scene->GetRegistry().remove<TerrainTraversal>(static_cast<entt::entity>(actor));
    position={8.5f,0,-3.7f};
    const auto base=scene->Raycast(position+glm::vec3(0,2,0),{0,-1,0},4);
    position.y=base->Position.y+.07f;
    const glm::vec3 start=position;
    bool slope=false;
    for(float sign : {1.f,-1.f})
        for(int i=0;i<40;++i)
            slope |= MoveCharacter(*scene,1,{sign*.025f,0,0},.2f,1.f/60)==TraversalMode::Slope;
    Check(slope && glm::distance(start,position)<.01f,"ascend and descend the PhysX heightfield at the sampled height");

    // 用正式维修站角色求脚踝、手腕的目标。脚踝上方有额外的小骨头，三根关节不必紧邻。
    const auto rig = ModelAnimation::Load("assets/game/station/crew/Operator.glb");
    Check(bool(rig),"load the station operator for IK");
    if (rig) CheckTerrainFeet(rig);
    if (rig) for (const ModelAnimation::Limb& limb : {
        ModelAnimation::Limb{"thigh.L","shin.L","foot.L"}, ModelAnimation::Limb{"upper_arm.L","forearm.L","hand.L"}}) {
        std::vector<ModelAnimation::NodePose> pose;
        std::vector<glm::mat4> world;
        rig->SamplePose(0,.22f,pose);
        rig->GetModelTransforms(pose,world);
        glm::mat4 upper,tip;
        rig->GetNodeTransform(limb.Upper,world,upper);
        rig->GetNodeTransform(limb.Tip,world,tip);
        glm::vec3 target=glm::mix(glm::vec3(upper[3]),glm::vec3(tip[3]),.82f);
        bool solved=rig->SolveLimb(pose,world,limb,target);
        rig->GetModelTransforms(pose,world);
        rig->GetNodeTransform(limb.Tip,world,tip);
        const float error=glm::distance(glm::vec3(tip[3]),target);
        std::cout<<limb.Tip<<" IK error (m) = "<<error<<"\n";
        Check(solved && error<.002f,"operator glTF reaches the requested limb position within 2 mm");
    }
    scene->OnSimulationStop();
}
