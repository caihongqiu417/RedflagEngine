#pragma once

#include "Redflag/Scene/Entity.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/SceneSerializer.h"
#include <cmath>

static void CheckLive2DBodies(void (*check)(bool, const char*))
{
    using namespace Redflag;
    Ref<Scene> room = CreateRef<Scene>();
    room->OnSimulationStart();
    Entity floor = room->CreateEntity("2D floor");
    floor.GetComponent<TransformComponent>().Translation.y = -.5f;
    floor.AddComponent<RigidBody2DComponent>();
    floor.AddComponent<BoxCollider2DComponent>().Size = { 4.f, .5f };
    Entity ball = room->CreateEntity("Ball");
    ball.GetComponent<TransformComponent>().Translation.y = 2.f;
    ball.AddComponent<RigidBody2DComponent>().Type = RigidBody2DComponent::BodyType::Dynamic;
    ball.AddComponent<CircleCollider2DComponent>();
    auto advance = [&](int steps) {
        for (int i = 0; i < steps; ++i)
            room->OnUpdatePhysics(1.f / 60.f);
    };
    advance(120);
    check(std::abs(ball.GetComponent<TransformComponent>().Translation.y - .5f) < .03f,
        "2D bodies and both collider types added during simulation collide");

    room->DuplicateEntity(ball);
    Entity copy;
    auto objects = room->GetRegistry().view<TagComponent, IDComponent>();
    for (entt::entity object : objects)
        if (objects.get<TagComponent>(object).Tag == "Ball" && objects.get<IDComponent>(object).ID != ball.GetUUID())
            copy = Entity(object, room.get());
    if (!copy)
    {
        check(false, "duplicate 2D ball");
        return;
    }
    copy.GetComponent<TransformComponent>().Translation = { 2.f, 2.f, 0.f };
    advance(120);
    check(std::abs(copy.GetComponent<TransformComponent>().Translation.x - 2.f) < .01f &&
        std::abs(copy.GetComponent<TransformComponent>().Translation.y - .5f) < .03f &&
        std::abs(ball.GetComponent<TransformComponent>().Translation.x) < .01f,
        "duplicated 2D body has its own position and collision shape");

    // 圆里再放一个较小的方形。移除圆后，球会落到小方形能接触地面的高度。
    ball.AddComponent<BoxCollider2DComponent>().Size = { .15f, .15f };
    advance(1);
    ball.RemoveComponent<CircleCollider2DComponent>();
    advance(45);
    check(std::abs(ball.GetComponent<TransformComponent>().Translation.y - .15f) < .03f &&
        copy.GetComponent<TransformComponent>().Translation.y > .49f,
        "removing one circle leaves its smaller box and the duplicate's circle intact");
    floor.RemoveComponent<BoxCollider2DComponent>();
    advance(45);
    check(copy.GetComponent<TransformComponent>().Translation.y < -.5f, "removing the floor box removes its support");

    floor.AddComponent<BoxCollider2DComponent>().Size = { 4.f, .5f };
    copy.RemoveComponent<RigidBody2DComponent>();
    copy.GetComponent<TransformComponent>().Translation.y = 2.f;
    copy.AddComponent<RigidBody2DComponent>().Type = RigidBody2DComponent::BodyType::Dynamic;
    advance(120);
    check(std::abs(copy.GetComponent<TransformComponent>().Translation.y - .5f) < .03f, "re-added body and box collide again");
    room->DestroyEntity(floor);
    advance(45);
    check(copy.GetComponent<TransformComponent>().Translation.y < -.5f, "destroying the floor leaves no ghost box");

    Ref<Scene> separate = Scene::Copy(room);
    room->OnSimulationStop();
    copy.GetComponent<TransformComponent>().Translation.y = 5.f;
    room->OnSimulationStart();
    advance(1);
    check(copy.GetComponent<TransformComponent>().Translation.y > 4.9f, "restarted 2D world uses the new starting pose");
    separate->OnSimulationStart();
    separate->OnUpdatePhysics(1.f / 60.f);
    check(separate->GetEntityByUUID(copy.GetUUID()).GetComponent<TransformComponent>().Translation.y < -.5f,
        "copying a running scene gives the second world independent 2D bodies");
    separate->OnSimulationStop();
    room->OnSimulationStop();
}

static void CheckPhysicsScene(void (*check)(bool, const char*))
{
    using namespace Redflag;
    Ref<Scene> original = CreateRef<Scene>();
    Entity floor = original->CreateEntity("Floor");
    floor.GetComponent<TransformComponent>().Translation.y = -.5f;
    floor.AddComponent<RigidBody3DComponent>();
    floor.AddComponent<BoxCollider3DComponent>().HalfSize = { 5.f, .5f, 5.f };

    Entity crate = original->CreateEntity("Crate");
    crate.GetComponent<TransformComponent>().Translation.y = 3.f;
    crate.GetComponent<TransformComponent>().Scale.x = -2.f;
    RigidBody3DComponent& body = crate.AddComponent<RigidBody3DComponent>();
    body.Type = BodyType3D::Dynamic;
    body.Density = 12.f;
    crate.AddComponent<BoxCollider3DComponent>();
    crate.AddComponent<PickupComponent>();

    Entity platform = original->CreateEntity("Platform");
    platform.GetComponent<TransformComponent>().Translation = { 3.f, 1.5f, 0.f };
    platform.AddComponent<RigidBody3DComponent>().Type = BodyType3D::Kinematic;
    platform.AddComponent<BoxCollider3DComponent>();

    Entity beam = original->CreateEntity("Beam");
    beam.GetComponent<TransformComponent>().Translation = { 8.f, 1.f, 0.f };
    beam.GetComponent<TransformComponent>().Rotation.y = glm::radians(90.f);
    beam.AddComponent<RigidBody3DComponent>();
    beam.AddComponent<BoxCollider3DComponent>().HalfSize = { 1.f, .25f, .1f };

    const char* path = "../bin-int/Tests/physics-scene.redflag";
    SceneSerializer(original).Serialize(path);
    Ref<Scene> edit = CreateRef<Scene>();
    if (!SceneSerializer(edit).Deserialize(path))
    {
        check(false, "reopen saved 3D physics scene");
        return;
    }
    const uint64_t crateID = crate.GetUUID(), floorID = floor.GetUUID(), platformID = platform.GetUUID();
    Entity editCrate = edit->GetEntityByUUID(crateID);
    check(editCrate.HasComponent<PickupComponent>() && !edit->GetEntityByUUID(floorID).HasComponent<PickupComponent>(),
        "only the marked crate is eligible for pickup after reopening");
    check(editCrate.GetComponent<RigidBody3DComponent>().Density == 12.f &&
        editCrate.GetComponent<RigidBody3DComponent>().Type == BodyType3D::Dynamic &&
        edit->GetEntityByUUID(floorID).GetComponent<BoxCollider3DComponent>().HalfSize.x == 5.f,
        "body type, density and local box dimensions survive YAML save/open");

    Ref<Scene> simulation = Scene::Copy(edit);
    simulation->OnSimulationStart();
    std::optional<RayHit3D> hit = simulation->Raycast({ .8f, 7.f, 0.f }, { 0.f, -1.f, 0.f }, 5.f);
    check(hit && hit->Entity == crateID && std::abs(hit->Distance - 3.5f) < .001f,
        "initial ray hits the scaled crate by its saved UUID");
    hit = simulation->Raycast({ 8.f, 2.f, .8f }, { 0.f, -1.f, 0.f }, 1.f);
    check(hit && hit->Entity == uint64_t(beam.GetUUID()) && std::abs(hit->Distance - .75f) < .001f,
        "saved rotation turns the narrow collision box with the beam");

    for (int i = 0; i < 180; ++i)
        simulation->OnUpdatePhysics(1.f / 60.f);
    Entity movingCrate = simulation->GetEntityByUUID(crateID);
    check(std::abs(movingCrate.GetComponent<TransformComponent>().Translation.y - .5f) < .03f,
        "PhysX writes the resting crate pose back to its scene transform");
    check(editCrate.GetComponent<TransformComponent>().Translation.y == 3.f,
        "the saved editor scene keeps its original pose");

    Entity movingPlatform = simulation->GetEntityByUUID(platformID);
    movingPlatform.GetComponent<TransformComponent>().Translation.x = 4.f;
    simulation->OnUpdatePhysics(1.f / 60.f);
    std::vector<uint64_t> nearby = simulation->OverlapSphere({ 4.f, 1.5f, 0.f }, .1f);
    check(nearby.size() == 1 && nearby.front() == platformID, "kinematic platform follows its scene transform");

    movingPlatform.RemoveComponent<BoxCollider3DComponent>();
    check(simulation->OverlapSphere({ 4.f, 1.5f, 0.f }, .1f).empty(), "removing the collider immediately clears its hit");
    movingPlatform.AddComponent<BoxCollider3DComponent>().HalfSize = { 1.f, .5f, .5f };
    simulation->OnUpdatePhysics(1.f / 60.f);
    check(!simulation->OverlapSphere({ 4.9f, 1.5f, 0.f }, .1f).empty(), "new collider uses its authored dimensions");
    movingPlatform.GetComponent<BoxCollider3DComponent>().HalfSize.x = .25f;
    simulation->OnUpdatePhysics(1.f / 60.f);
    check(simulation->OverlapSphere({ 4.9f, 1.5f, 0.f }, .1f).empty(), "editing the shape removes its previous extent");
    simulation->DestroyEntity(movingPlatform);
    check(simulation->OverlapSphere({ 4.f, 1.5f, 0.f }, .1f).empty(), "destroyed entities disappear from queries");

    simulation->DuplicateEntity(movingCrate);
    Entity copy;
    auto crates = simulation->GetRegistry().view<TagComponent, IDComponent>();
    for (entt::entity handle : crates)
    {
        if (crates.get<TagComponent>(handle).Tag == "Crate" && uint64_t(crates.get<IDComponent>(handle).ID) != crateID)
            copy = Entity(handle, simulation.get());
    }
    if (copy)
    {
        copy.GetComponent<TransformComponent>().Translation = { -3.f, 4.f, 0.f };
        simulation->OnUpdatePhysics(1.f / 60.f);
        hit = simulation->Raycast({ -3.f, 7.f, 0.f }, { 0.f, -1.f, 0.f }, 5.f);
    }
    check(copy && copy.HasComponent<PickupComponent>(), "duplicating the crate keeps its pickup tag");
    check(copy && hit && hit->Entity == uint64_t(copy.GetUUID()), "the duplicated crate has its own physics body and UUID");
    simulation->OnSimulationStop();
    check(!simulation->Raycast({ 0.f, 7.f, 0.f }, { 0.f, -1.f, 0.f }, 10.f), "stopping physics releases the queried world");

    Ref<Scene> old = CreateRef<Scene>();
    Entity flat = old->CreateEntity("2D");
    flat.AddComponent<RigidBody2DComponent>().Type = RigidBody2DComponent::BodyType::Dynamic;
    flat.AddComponent<BoxCollider2DComponent>();
    const char* oldPath = "../bin-int/Tests/physics-2d.redflag";
    SceneSerializer(old).Serialize(oldPath);
    Ref<Scene> reopened = CreateRef<Scene>();
    if (!SceneSerializer(reopened).Deserialize(oldPath))
    {
        check(false, "open a scene using the older 2D body fields");
        return;
    }
    reopened->OnSimulationStart();
    reopened->OnUpdatePhysics(1.f / 60.f);
    check(reopened->GetEntityByUUID(flat.GetUUID()).GetComponent<TransformComponent>().Translation.y < 0.f &&
        reopened->OverlapSphere({ 0.f, 0.f, 0.f }, 5.f).empty(), "2D-only scenes still fall in Box2D, with no 3D hits");
    reopened->OnSimulationStop();
    CheckLive2DBodies(check);
}
