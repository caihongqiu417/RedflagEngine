#include "rfpch.h"
#include "NpcBindings.h"
#include "NpcMemoryBindings.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scene/Scene.h"
#include <mono/metadata/appdomain.h>
#include <mono/metadata/object.h>

namespace Redflag
{
	namespace
	{
		NpcSystem* Actors()
		{
			Scene* scene = ScriptEngine::GetSceneContext();
			return scene ? scene->GetNpcs() : nullptr;
		}

		bool Submit(uint64_t actor, MonoString* instruction)
		{
			NpcSystem* actors = Actors();
			if (!actors || !instruction) return false;

			char* text = mono_string_to_utf8(instruction);
			const bool accepted = actors->Submit(actor, text);
			mono_free(text);
			return accepted;
		}

		bool Cancel(uint64_t actor)
		{
			NpcSystem* actors = Actors();
			return actors && actors->Cancel(actor);
		}

		bool ReadEvent(uint64_t actor, uint64_t* cursor, double* seconds, uint64_t* target,
			MonoString** kind, MonoString** text, MonoString** speech)
		{
			NpcSystem* actors = Actors();
			const NpcEvent* event = actors ? actors->NextEvent(actor, *cursor) : nullptr;
			if (!event) return false;

			*seconds = event->Time;
			*target = event->Target;
			MonoDomain* domain = mono_domain_get();
			*kind = mono_string_new(domain, event->Kind.c_str());
			*text = mono_string_new(domain, event->Text.c_str());
			*speech = mono_string_new(domain, event->Speech.c_str());
			return true;
		}
	}

	void RegisterNpcCalls()
	{
		RegisterNpcMemoryCalls();
		mono_add_internal_call("Redflag.Npc::Submit", (const void*)Submit);
		mono_add_internal_call("Redflag.Npc::Cancel", (const void*)Cancel);
		mono_add_internal_call("Redflag.Npc::ReadEvent", (const void*)ReadEvent);
	}
}
