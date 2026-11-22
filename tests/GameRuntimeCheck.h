#pragma once
#include "Redflag/Scene/ScriptableEntity.h"

namespace {
    struct NativeCounter : ScriptableEntity
    {
        inline static int Created = 0, Destroyed = 0, Released = 0;
        void OnCreate() override { ++Created; }
        void OnDestroy() override { ++Destroyed; }
        ~NativeCounter() override { ++Released; }
    };

    void CheckRuntimeCopy()
    {
        Ref<Scene> edit = CreateRef<Scene>();
        Entity target = edit->CreateEntityWithUUID(401,"Target");
        target.GetComponent<TransformComponent>().Translation.x = 3.f;
        Entity actor = edit->CreateEntityWithUUID(402,"Chaser");
        ScriptComponent& script = actor.AddComponent<ScriptComponent>();
        script.ClassName = "Sandbox.Chaser";
        const uint64_t targetID = target.GetUUID();
        std::vector<uint8_t>& bytes = script.Fields["Target"];
        bytes.resize(sizeof(targetID));
        std::memcpy(bytes.data(),&targetID,sizeof(targetID));
        actor.AddComponent<NativeScriptComponent>().Bind<NativeCounter>();
        actor.AddComponent<MeshRendererComponent>().Mat = CreateRef<Material>();

        Ref<Scene> game = Scene::Copy(edit);
        Entity playing = game->GetEntityByUUID(402);
        game->OnRuntimeStart();
        game->UpdateGame(1.f/60.f);
        Check(playing.GetComponent<TransformComponent>().Translation.x > 0.f &&
              actor.GetComponent<TransformComponent>().Translation.x == 0.f,"C# updates must move only the playing entity");
        Ref<Material>& live = playing.GetComponent<MeshRendererComponent>().Mat;
        live->BaseColor = {1,0,0,1};
        Check(actor.GetComponent<MeshRendererComponent>().Mat->BaseColor == glm::vec4(1),"Editing a play material changed the saved one");
        game->DuplicateEntity(playing);
        game->UpdateGame(1.f/60.f);
        Check(NativeCounter::Created == 2,"Duplicated entities must create their own native script");

        game->DestroyEntity(game->GetEntityByUUID(401));
        uint64_t runtimeTarget, editorTarget;
        std::memcpy(&runtimeTarget,playing.GetComponent<ScriptComponent>().Fields.at("Target").data(),sizeof(runtimeTarget));
        std::memcpy(&editorTarget,bytes.data(),sizeof(editorTarget));
        Check(runtimeTarget == 0 && editorTarget == targetID,"Deleting a play target lost the editable Entity reference");
        float x = playing.GetComponent<TransformComponent>().Translation.x;
        game->UpdateGame(1.f/60.f);
        Check(playing.GetComponent<TransformComponent>().Translation.x == x,"Managed script still read the deleted target");
        game->DestroyEntity(playing);
        Check(NativeCounter::Destroyed == 1 && NativeCounter::Released == 1,"Deleted native script was not destroyed exactly once");
        game->OnRuntimeStop();
        Check(NativeCounter::Destroyed == 2 && NativeCounter::Released == 2 && !ScriptEngine::IsRuntimeActive(),
              "Stopping left a native instance or a Mono scene active");
    }
}
