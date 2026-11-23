#include "rfpch.h"
#include "MontageBindings.h"
#include "Redflag/Animation/MontageRuntime.h"
#include "Redflag/Scene/Entity.h"
#include <mono/metadata/appdomain.h>
#include <mono/metadata/object.h>

namespace Redflag
{
	namespace
	{
		Entity Actor(uint64_t id) { return ScriptEngine::GetSceneContext()->GetEntityByUUID(id); }

		std::string Text(MonoString* value)
		{
			char* utf8 = mono_string_to_utf8(value);
			std::string result(utf8);
			mono_free(utf8);
			return result;
		}

		bool Play(uint64_t actor, MonoString* file, MonoString* section, bool restart)
		{
			return PlayMontageFile(Actor(actor),Text(file),Text(section),restart);
		}

		bool Jump(uint64_t actor, MonoString* slot, MonoString* section)
		{
			MontagePlayer* player = GetMontagePlayer(Actor(actor));
			return player && player->Jump(Text(slot),Text(section));
		}

		bool Stop(uint64_t actor, MonoString* slot)
		{
			MontagePlayer* player = GetMontagePlayer(Actor(actor));
			return player && player->Stop(Text(slot));
		}

		bool IsPlaying(uint64_t actor, MonoString* slot)
		{
			const MontagePlayer* player = GetMontagePlayer(Actor(actor));
			return player && player->IsPlaying(Text(slot));
		}

		bool IsOpen(uint64_t actor, MonoString* slot, MonoString* window)
		{
			const MontagePlayer* player = GetMontagePlayer(Actor(actor));
			return player && player->IsOpen(Text(slot),Text(window));
		}

		bool SetWarpTarget(uint64_t id, MonoString* name, glm::vec3* position, float yawRadians)
		{
			Entity actor = Actor(id);
			if (!actor) return false;
			AddMontagePlayer(actor).SetTarget(Text(name),{*position,yawRadians});
			return true;
		}

		bool ClearWarpTarget(uint64_t id, MonoString* name)
		{
			MontagePlayer* player = GetMontagePlayer(Actor(id));
			return player && player->ClearTarget(Text(name));
		}

		bool Read(uint64_t actor, MonoString** slot, MonoString** section, MonoString** state, MonoString** name,
			int* phase, float* seconds, uint8_t* interrupted)
		{
			MontagePlayer* player = GetMontagePlayer(Actor(actor));
			MontageEvent event;
			if (!player || !player->Read(player->ScriptCursor,event)) return false;

			MonoDomain* domain = mono_domain_get();
			*slot = mono_string_new(domain,event.Slot.c_str());
			*section = mono_string_new(domain,event.Section.c_str());
			*state = mono_string_new(domain,event.Notice.State.c_str());
			*name = mono_string_new(domain,event.Notice.Name.c_str());
			*phase = int(event.Notice.Phase);
			*seconds = event.Notice.Seconds;
			*interrupted = event.Notice.Interrupted;
			return true;
		}
	}

	void RegisterMontageBindings()
	{
		mono_add_internal_call("Redflag.Montage::Play",(const void*)Play);
		mono_add_internal_call("Redflag.Montage::Jump",(const void*)Jump);
		mono_add_internal_call("Redflag.Montage::Stop",(const void*)Stop);
		mono_add_internal_call("Redflag.Montage::IsPlaying",(const void*)IsPlaying);
		mono_add_internal_call("Redflag.Montage::IsOpen",(const void*)IsOpen);
		mono_add_internal_call("Redflag.Montage::SetWarpTarget",(const void*)SetWarpTarget);
		mono_add_internal_call("Redflag.Montage::ClearWarpTarget",(const void*)ClearWarpTarget);
		mono_add_internal_call("Redflag.Montage::Read",(const void*)Read);
	}
}
