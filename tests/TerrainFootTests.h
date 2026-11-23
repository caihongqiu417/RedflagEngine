#pragma once

#include "Redflag/Gameplay/TraversalRig.h"
#include <yaml-cpp/yaml.h>

inline void CheckTerrainFeet(const Ref<ModelAnimation>& model)
{
    const auto& clips = model->GetClips();
    const auto clipIndex = [&](const char* name) {
        return size_t(std::find_if(clips.begin(), clips.end(), [&](const auto& c){return c.Name == name;})-clips.begin());
    };
    TraversalRig rig;
    rig.MovementRoot = "root";
    rig.Limbs[0] = {"thigh.L","shin.L","foot.L"};
    rig.Limbs[1] = {"thigh.R","shin.R","foot.R"};
    const YAML::Node contacts=YAML::LoadFile("../output/validation/station-actions/sole-contacts.json");
    for (size_t i=0;i<2;++i) for (const auto& point : contacts[i==0 ? "L6":"R6"]["points"]) {
        const auto xyz=point["xyz"].as<std::vector<float>>();
        rig.Soles[i].push_back({point["bone"].as<std::string>(),{xyz[0],xyz[1],xyz[2]}});
    }
    TerrainTraversal motion;
    motion.HasSupport = true;
    motion.Clearance = .07f;

    auto box = [](Scene& scene, uint64_t id, glm::vec3 center, glm::vec3 half, float yaw=0.f) {
        Entity body=scene.CreateEntityWithUUID(id,"Support");
        auto& transform=body.GetComponent<TransformComponent>();
        transform.Translation=center;
        transform.Rotation.y=yaw;
        body.AddComponent<RigidBody3DComponent>();
        body.AddComponent<BoxCollider3DComponent>().HalfSize=half;
    };
    auto foot = [&](const std::vector<ModelAnimation::NodePose>& pose,const TransformComponent& actor,const char* name) {
        std::vector<glm::mat4> world;
        model->GetModelTransforms(pose,world);
        glm::mat4 joint;
        if (!model->GetNodeTransform(name,world,joint)) throw std::runtime_error(name);
        return glm::vec3(actor.GetTransform()*joint[3]);
    };

    // 发行包 b6332574 的 #74～76，位置、朝向和剪辑时间来自实际玩家输入的逐帧记录。
    const struct Frame { int Id; float X,Z,Yaw,Time; } frames[]={
        {74,-3.47835732f,2.52604365f,1.07817662f,.31285122f},
        {75,-3.11917734f,2.59821630f,1.24226880f,.414617717f},
        {76,-2.74638724f,2.67312360f,1.31659520f,.520240307f}
    };
    Scene scene;
    box(scene,1121,{0,-.14f,0},{10,.15f,5.8f});
    Entity chest=scene.CreateEntityWithUUID(1112,"Chest");
    auto& chestTransform=chest.GetComponent<TransformComponent>();
    chestTransform.Translation={-3.4f,.08f,2.15f};
    chestTransform.Rotation.y=-.24f;
    chest.AddComponent<RigidBody3DComponent>();
    auto& shape=chest.AddComponent<BoxCollider3DComponent>();
    shape.HalfSize={.39f,.23f,.30f};
    shape.Center={0,.23f,0};
    scene.OnSimulationStart();
    for (const Frame& frame:frames) {
        TransformComponent actor;
        actor.Translation={frame.X,.08f,frame.Z};
        actor.Rotation.y=frame.Yaw;
        std::vector<ModelAnimation::NodePose> pose;
        model->SamplePose(clipIndex("running_gun-cycle"),frame.Time,pose);
        model->PinMovementRoot(pose,"root");
        const glm::vec3 ankle=foot(pose,actor,"foot.L"), toe=foot(pose,actor,"toe.L");
        const RayHit3D inside=scene.Raycast({ankle.x,.39f,ankle.z},{0,-1,0},.76f,1101).value();
        const RayHit3D top=scene.Raycast({ankle.x,2.f,ankle.z},{0,-1,0},2.f,1101).value();
        Check(inside.Entity==1112 && inside.Distance==0.f && top.Entity==1112 && std::abs(top.Position.y-.54f)<.001f,
              "a ray from inside the toolbox returns its own starting point");
        AdjustTerrainPose(scene,1101,actor,rig,motion,*model,pose);
        const glm::vec3 finalToe=foot(pose,actor,"toe.L");
        Check(glm::distance(toe,finalToe)<.001f,"running past the high toolbox keeps the authored foot pose");
        std::cout<<"capture "<<frame.Id<<" ankle x/z "<<ankle.x<<" "<<ankle.z<<" ray distance "
                 <<inside.Distance<<" box top "<<top.Position.y<<" toe before/after "<<toe.y<<" "<<finalToe.y<<" m\n";
    }
    scene.OnSimulationStop();

    // 左脚下单独放一个踏面。模型中心所在的地面为零，脚下可高可低。
    for (const float height : {.20f,-.03f}) {
        Scene tread;
        box(tread,2,{0,height-.1f,0},{3,.1f,3});
        tread.OnSimulationStart();
        TransformComponent actor;
        actor.Translation.y=.07f;
        for (bool swinging : {false,true}) {
            std::vector<ModelAnimation::NodePose> pose;
            // 下坡用屈膝的拾取姿势，给脚掌留出真实可达的下降距离。
            const char* clip=swinging ? "running_gun-cycle" : height<0.f ? "Station-Pickup" : "Idle-cycle";
            model->SamplePose(clipIndex(clip),swinging ? .52f : height<0.f ? .5f : 0.f,pose);
            model->PinMovementRoot(pose,"root");
            const glm::vec3 ankle=foot(pose,actor,"foot.L"),toe=foot(pose,actor,"toe.L");
            AdjustTerrainPose(tread,1,actor,rig,motion,*model,pose);
            const glm::vec3 finalAnkle=foot(pose,actor,"foot.L"),finalToe=foot(pose,actor,"toe.L");
            const float moved=finalAnkle.y-ankle.y;
            const float targetRise=height>0.f ? height-(actor.Translation.y+.000322f) : height;
            Check(std::abs(moved-(swinging ? 0.f : targetRise))<.002f,
                swinging ? "foot already clear stays at its animated height" : "supporting ankle follows the tread");
            Check(glm::distance(finalToe-finalAnkle,toe-ankle)<.002f,"adjusting the knee preserves the shoe angle");
            std::cout<<"tread "<<height<<" m swing "<<swinging<<" ankle adjustment "<<moved<<" m\n";
        }
        if (height>0.f) {
            // 抬跟时踝骨已升高 20 厘米，但真正的靴底仍在模型原点上方约 1 厘米。
            for (size_t side=0;side<2;++side) {
                std::vector<ModelAnimation::NodePose> pose;
                model->SamplePose(clipIndex("walking_gun-cycle"),side==0 ? 1.016667f : .325f,pose);
                model->PinMovementRoot(pose,"root");
                const char* shoe=side==0 ? "foot.L" : "foot.R";
                const glm::vec3 ankle=foot(pose,actor,shoe);
                AdjustTerrainPose(tread,1,actor,rig,motion,*model,pose);
                const float moved=foot(pose,actor,shoe).y-ankle.y;
                // 全靴蒙皮顶点在两个时刻的最低 Y（模型坐标）；加实体高度和本次 IK 平移后复核台面。
                const float meshBottom=actor.Translation.y+(side==0 ? .011124482f : .017581312f)+moved;
                Check(std::abs(meshBottom-height)<.002f,"the rolling forefoot rests on the 20 cm tread");
                std::cout<<shoe<<" heel raised, ankle adjustment "<<moved<<" m, estimated sole "<<meshBottom<<" m\n";
            }
            std::vector<ModelAnimation::NodePose> pose;
            model->SamplePose(clipIndex("running_gun-cycle"),0.f,pose);
            model->PinMovementRoot(pose,"root");
            const glm::vec3 toe=foot(pose,actor,"toe.L");
            AdjustTerrainPose(tread,1,actor,rig,motion,*model,pose);
            Check(glm::distance(foot(pose,actor,"toe.L"),toe)<.001f,
                  "a shoe already 3.5 cm above the tread needs no extra lift");
        }
        if (height<0.f) {
            std::vector<ModelAnimation::NodePose> pose;
            model->SamplePose(clipIndex("Idle-cycle"),0.f,pose);
            model->PinMovementRoot(pose,"root");
            const glm::vec3 hip=foot(pose,actor,"thigh.L"), knee=foot(pose,actor,"shin.L"), ankle=foot(pose,actor,"foot.L");
            const float length=glm::distance(hip,knee)+glm::distance(knee,ankle);
            const glm::vec3 target=ankle+glm::vec3(0,height,0);
            AdjustTerrainPose(tread,1,actor,rig,motion,*model,pose);
            const glm::vec3 result=foot(pose,actor,"foot.L");
            Check(glm::distance(hip,target)>length && glm::distance(result,hip+length*glm::normalize(target-hip))<.002f,
                  "the extended leg reaches its length limit without stretching the shoe to the lower tread");
        }
        tread.OnSimulationStop();
    }
}
