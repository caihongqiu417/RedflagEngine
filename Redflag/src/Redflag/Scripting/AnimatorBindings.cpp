#include "rfpch.h"
#include "AnimatorBindings.h"
#include "Redflag/Animation/ActorAnimator.h"
#include "Redflag/Scene/Entity.h"
#include <mono/metadata/appdomain.h>
#include <mono/metadata/object.h>

namespace Redflag
{
	namespace
	{
		std::string Text(MonoString* s)
		{
			char* data=mono_string_to_utf8(s);
			std::string text(data);
			mono_free(data);
			return text;
		}

		Entity Actor(uint64_t id) { return ScriptEngine::GetSceneContext()->GetEntityByUUID(id); }
		ActorAnimator* Animator(Entity actor)
		{
			if (!actor) return nullptr;
			return ScriptEngine::GetSceneContext()->GetRegistry().try_get<ActorAnimator>(static_cast<entt::entity>(actor));
		}

		bool Bind(uint64_t id, MonoString* file)
		{
			Entity actor=Actor(id);
			if (!actor) return false;
			auto& animator=ScriptEngine::GetSceneContext()->GetRegistry().get_or_emplace<ActorAnimator>(static_cast<entt::entity>(actor));
			std::string error;
			if (!animator.Controller.Load(std::filesystem::u8path(Text(file)),error)) {
				RF_CORE_WARN("Animator: {0}",error);
				return false;
			}
			animator.Timeline.Cancel();
			return true;
		}

		bool Play(uint64_t id, MonoString* state, bool restart)
		{
			Entity actor=Actor(id);
			ActorAnimator* animator=Animator(actor);
			return animator && animator->Play(actor,Text(state),restart);
		}

		bool Read(uint64_t id, MonoString** state, MonoString** name, int* phase, float* seconds, uint8_t* interrupted)
		{
			ActorAnimator* animator=Animator(Actor(id));
			AnimationNotice notice;
			if (!animator || !animator->Timeline.Read(notice)) return false;
			*state=mono_string_new(mono_domain_get(),notice.State.c_str());
			*name=mono_string_new(mono_domain_get(),notice.Name.c_str());
			*phase=int(notice.Phase);
			*seconds=notice.Seconds;
			*interrupted=notice.Interrupted;
			return true;
		}

		bool IsOpen(uint64_t id, MonoString* name)
		{
			ActorAnimator* animator=Animator(Actor(id));
			return animator && animator->Timeline.IsOpen(Text(name));
		}

		bool IsPlaying(uint64_t id)
		{
			ActorAnimator* animator=Animator(Actor(id));
			return animator && animator->Timeline.Playing();
		}
	}

	void RegisterAnimatorBindings()
	{
		mono_add_internal_call("Redflag.Animator::Bind",(const void*)Bind);
		mono_add_internal_call("Redflag.Animator::Play",(const void*)Play);
		mono_add_internal_call("Redflag.Animator::Read",(const void*)Read);
		mono_add_internal_call("Redflag.Animator::IsOpen",(const void*)IsOpen);
		mono_add_internal_call("Redflag.Animator::IsPlaying",(const void*)IsPlaying);
	}
}
