#pragma once

#include "Redflag/Scene/NpcVision.h"

static void CheckNpcPickup(void (*check)(bool, const char*), uint16_t visionPort)
{
    using namespace Redflag;
    Scene room;
    Entity crate = room.CreateEntityWithUUID(103, "Crate");
    crate.AddComponent<RigidBody3DComponent>();
    crate.AddComponent<BoxCollider3DComponent>();
    crate.AddComponent<PickupComponent>();
    room.OnSimulationStart();
    const glm::vec3 hand(0, 0, 1);
    uint64_t id = crate.GetUUID();
    if (visionPort)
    {
        auto reply = AskNpcVision("Physics-Workshop", "Find the red crate.", visionPort);
        auto picked = reply.get();
        check(picked && *picked == id, "visual model selected red crate from baked images");
        if (!picked) return;
        id = *picked;
    }
    check(CanPickUp(room, id, hand, 1.0f), "crate within reach");
    check(!CanPickUp(room, id, hand, 0.3f), "crate beyond arm length");
    Entity wall = room.CreateEntityWithUUID(102, "Divider");
    wall.GetComponent<TransformComponent>().Translation.z = .75f;
    wall.GetComponent<TransformComponent>().Scale.z = .1f;
    wall.AddComponent<RigidBody3DComponent>();
    wall.AddComponent<BoxCollider3DComponent>();
    room.OnUpdatePhysics(1.0f / 60.0f);
    check(!CanPickUp(room, id, hand, 1.0f), "wall occludes crate");
    check(!CanPickUp(room, 102, hand, 1.0f), "wall has no pickup permission");
    room.DestroyEntity(crate);
    check(!CanPickUp(room, id, hand, 1.0f), "old photo of removed crate");
    room.OnSimulationStop();
}
