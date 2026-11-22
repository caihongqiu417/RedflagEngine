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

        NpcVisionCandidate region = AskNpcCandidate("Physics-Workshop", "请指出地台右侧的灰色台面。", visionPort).get();
        check(!region.Entity && region.Region && region.Region->Camera == "val-00" && region.Region->ID == "right-surface",
              "visual model selected indexed area without a pickup entity");
        if (region.Region)
        {
            // 实验台顶面在 y=0。用这个已知平面验证照片射线，运行时不会拿它猜测目标深度。
            const NpcViewRegion& area = *region.Region;
            const glm::vec3 hit = area.Origin - area.Direction * (area.Origin.y / area.Direction.y);
            check(std::abs(glm::length(area.Direction) - 1.f) < 1e-5f && hit.x > 1.f && hit.x < 3.f &&
                  std::abs(hit.z) < 2.f, "photo ray crosses the right half of the original platform");
        }
        NpcVisionCandidate crateAnswer = AskNpcCandidate("Physics-Workshop", "右边那个红箱子是哪一个？", visionPort).get();
        check(crateAnswer.Entity == id && !crateAnswer.Region, "a location word in a pickup question still selects the crate");
        NpcVisionCandidate other = AskNpcCandidate("Different-Room", "Find the right side.", visionPort).get();
        check(!other.Entity && !other.Region, "another room cannot return this camera or entity");
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
