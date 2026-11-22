#include "rfpch.h"
#include "NpcMemoryBindings.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/AI/EnvironmentSense.h"
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

		NpcMemory* Notes(uint64_t actor)
		{
			NpcSystem* system = Actors();
			return system ? system->GetMemory(actor) : nullptr;
		}

		std::string Text(MonoString* source)
		{
			if (!source) return {};
			char* chars = mono_string_to_utf8(source);
			std::string result(chars);
			mono_free(chars);
			return result;
		}

		MonoString* Managed(const std::string& value)
		{
			return mono_string_new(mono_domain_get(), value.c_str());
		}

		// ---- 观察与转述 ----
		MonoArray* ReadEnvironment(uint64_t actor)
		{
			NpcSystem* actors = Actors();
			const NpcDefinition* role = actors ? actors->GetDefinition(actor) : nullptr;
			std::vector<DecisionFact> facts;
			if (role && role->ObserveWorld) facts = SenseEnvironment(*ScriptEngine::GetSceneContext(), actor);

			MonoArray* pairs = mono_array_new(mono_domain_get(), mono_get_string_class(), facts.size() * 2);
			for (size_t i = 0; i < facts.size(); ++i)
			{
				mono_array_setref(pairs, i * 2, Managed(facts[i].ID));
				mono_array_setref(pairs, i * 2 + 1, Managed(facts[i].Description));
			}
			return pairs;
		}

		bool Report(uint64_t actor, uint64_t target, MonoString* text, MonoString* image)
		{
			NpcSystem* system = Actors();
			return system && system->Report(actor, target, Text(text), Text(image));
		}

		bool ReportPlace(uint64_t actor, const glm::vec3* place, MonoString* text)
		{
			NpcSystem* system = Actors();
			NpcMemory* notes = Notes(actor);
			return notes && notes->Report(0, Text(text), system->SessionSeconds(), {}, *place);
		}

		bool Read(uint64_t actor, int index, int* source, uint64_t* target, MonoString** text, MonoString** image, double* seconds)
		{
			NpcMemory* notes = Notes(actor);
			if (!notes || index < 0 || size_t(index) >= notes->GetEvidence().size()) return false;
			const NpcEvidence& e = notes->GetEvidence()[index];
			*source = int(e.Source);
			*target = e.Target;
			*seconds = e.Seconds;
			*text = Managed(e.Text);
			*image = Managed(e.Image);
			return true;
		}

		// ---- 偏好与存档 ----
		bool SetPreference(uint64_t actor, MonoString* key, MonoString* value)
		{
			NpcMemory* notes = Notes(actor);
			return notes && notes->SetPreference(Text(key), Text(value));
		}

		MonoString* GetPreference(uint64_t actor, MonoString* key)
		{
			NpcMemory* notes = Notes(actor);
			const std::string* value = notes ? notes->GetPreference(Text(key)) : nullptr;
			return value ? Managed(*value) : nullptr;
		}

		MonoString* SavePreferences(uint64_t actor)
		{
			NpcMemory* notes = Notes(actor);
			return notes ? Managed(notes->SavePreferences()) : nullptr;
		}

		bool LoadPreferences(uint64_t actor, MonoString* save)
		{
			NpcMemory* notes = Notes(actor);
			return notes && notes->LoadPreferences(Text(save));
		}
	} // namespace

	void RegisterNpcMemoryCalls()
	{
		mono_add_internal_call("Redflag.NpcSense::ReadEnvironment", (const void*)ReadEnvironment);
		mono_add_internal_call("Redflag.NpcNotes::Report", (const void*)Report);
		mono_add_internal_call("Redflag.NpcNotes::ReportPlace", (const void*)ReportPlace);
		mono_add_internal_call("Redflag.NpcNotes::Read", (const void*)Read);
		mono_add_internal_call("Redflag.NpcNotes::SetPreference", (const void*)SetPreference);
		mono_add_internal_call("Redflag.NpcNotes::GetPreference", (const void*)GetPreference);
		mono_add_internal_call("Redflag.NpcNotes::SavePreferences", (const void*)SavePreferences);
		mono_add_internal_call("Redflag.NpcNotes::LoadPreferences", (const void*)LoadPreferences);
	}
} // namespace Redflag
