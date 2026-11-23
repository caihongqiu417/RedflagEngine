#pragma once

inline void CheckGroundedColliders()
{
    Ref<Scene> scene = CreateRef<Scene>();
    Entity floor = scene->CreateEntityWithUUID(100, "Floor");
    floor.GetComponent<TransformComponent>().Translation.y = -.5f;
    floor.AddComponent<RigidBody3DComponent>();
    floor.AddComponent<BoxCollider3DComponent>().HalfSize = {4,.5f,4};

    Entity actor = scene->CreateEntityWithUUID(101,"Capsule");
    auto& tr = actor.GetComponent<TransformComponent>();
    tr.Translation.y = .08f;
    actor.AddComponent<RigidBody3DComponent>().Type = BodyType3D::Kinematic;
    auto& capsule = actor.AddComponent<CapsuleCollider3DComponent>();
    capsule.Radius = .3f;
    capsule.Height = 1.8f;
    capsule.Center.y = .9f;
    scene->OnSimulationStart();

    auto step = [&] {
        MoveCharacter(*scene,101,{0,0,0},capsule.Radius,1.f/60);
        scene->OnUpdatePhysics(1.f/60);
    };
    auto hitActor = [&](glm::vec3 origin, glm::vec3 direction, float distance) {
        const auto hit = scene->Raycast(origin, direction, distance);
        return hit && hit->Entity == 101;
    };
    step();
    Check(std::abs(tr.Translation.y) < .001f, "capsule feet settle on the floor despite an 8 cm spawn gap");
    Check(hitActor({0,2,0},{0,-1,0},.21f) && !hitActor({.28f,1.77f,-1},{0,0,1},2),
        "ray hits the crown but misses the empty upper corner of a capsule");
    const auto corner = scene->OverlapSphere({.29f,1.79f,0},.01f);
    Check(std::find(corner.begin(),corner.end(),101) == corner.end(), "rounded cap also excludes the corner in an overlap query");

    tr.Scale = {1.25f,1.5f,.75f};
    tr.Translation.y = 1.42f;
    capsule.Center = {0,0,0};
    step();
    Check(std::abs(tr.Translation.y - 1.35f) < .001f, "scaled centre-origin capsule puts its lower tip on the floor");

    tr.Scale = {1,1,1};
    tr.Translation.y = .08f;
    capsule.Axis = CapsuleAxis::Z;
    capsule.Center.y = capsule.Radius;
    step();
    Check(std::abs(tr.Translation.y) < .001f && hitActor({0,1,.65f},{0,-1,0},1) &&
        !hitActor({0,1.2f,-2},{0,0,1},4), "prone capsule lies on its side and a chest-high ray passes above");
    capsule.Axis = CapsuleAxis::X;
    step();
    Check(hitActor({.65f,1,0},{0,-1,0},1) && !hitActor({0,1,.65f},{0,-1,0},.9f),
        "changing the local axis rotates the physical capsule");

    const char* path = "../bin-int/Tests/ground-capsule.redflag";
    SceneSerializer(scene).Serialize(path);
    Ref<Scene> saved = CreateRef<Scene>();
    Check(SceneSerializer(saved).Deserialize(path), "read back the capsule scene");
    for (Ref<Scene> copy : {saved, Scene::Copy(scene)}) {
        const auto& shape = copy->GetEntityByUUID(101).GetComponent<CapsuleCollider3DComponent>();
        Check(shape.Axis == CapsuleAxis::X && std::abs(shape.Height-1.8f)<.001f &&
            std::abs(shape.Radius-.3f)<.001f && shape.Center == capsule.Center,
            "saved and duplicated scenes retain capsule dimensions, centre and axis");
    }
    actor.RemoveComponent<CapsuleCollider3DComponent>();
    Check(!hitActor({0,.3f,-2},{0,0,1},4), "removing the capsule clears the physical shape");

    // 旧场景常将模型原点放在盒子中心，盒子的半高也要随实体缩放。
    auto& box = actor.AddComponent<BoxCollider3DComponent>();
    box.HalfSize = {.25f,.5f,.25f};
    tr.Scale = {1,2,1};
    tr.Translation.y = 1.09f;
    MoveCharacter(*scene,101,{0,0,0},.25f,1.f/60);
    Check(std::abs(tr.Translation.y - 1.f) < .001f, "centre-origin box keeps the half-height but drops the spawn gap");
    scene->OnSimulationStop();
}
