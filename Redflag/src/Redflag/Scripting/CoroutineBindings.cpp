#include "rfpch.h"
#include "CoroutineBindings.h"
#include "ScriptEngine.h"
#include "Redflag/Assets/AssetUpdate.h"
#include "Redflag/Core/Application.h"
#include <filesystem>
#include <fstream>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/object.h>

namespace Redflag
{
    namespace
    {
        struct TextResult
        {
            std::string Text, Error;
        };

        uint64_t s_NextRead = 0;
        std::unordered_map<uint64_t, AssetUpdate<TextResult>> s_Reads;

        void SetActive(uint64_t id, mono_bool active)
        {
            if (Ref<ScriptInstance> instance = ScriptEngine::GetEntityScriptInstance(id))
                instance->SetCoroutinesActive(active != 0);
        }

        TextResult ReadFile(const std::filesystem::path& assets, const std::string& name)
        {
            namespace fs = std::filesystem;
            std::error_code error;
            const fs::path path = fs::weakly_canonical(assets / fs::u8path(name), error);
            const fs::path relative = path.lexically_relative(assets);
            if (error || relative.empty() || *relative.begin() == ".." || !fs::is_regular_file(path, error))
                return {{}, "Asset file not found in assets: " + name};
            std::ifstream input(path, std::ios::binary);
            if (!input) return {{}, "Cannot open asset: " + name};
            std::ostringstream text;
            text << input.rdbuf();
            if (input.bad()) return {{}, "Cannot read asset: " + name};
            return {text.str(), {}};
        }

        uint64_t Begin(MonoString* filename)
        {
            char* utf8 = filename ? mono_string_to_utf8(filename) : nullptr;
            std::string name = utf8 ? utf8 : "";
            if (utf8) mono_free(utf8);
            const std::filesystem::path assets = std::filesystem::weakly_canonical("assets");
            const uint64_t id = ++s_NextRead;
            s_Reads[id].Request(Application::Get().GetJobs(), RF_JOB("Read script text asset"), RF_JOB("Script text ready"),
                [assets, name] { return ReadFile(assets, name); },
                [](TextResult result) { return CreateRef<TextResult>(std::move(result)); });
            return id;
        }

        mono_bool Poll(uint64_t id, MonoString** text, MonoString** error)
        {
            const auto result = s_Reads.at(id).Get();
            if (!result) return false;
            MonoDomain* domain = mono_domain_get();
            *error = result->Error.empty() ? nullptr : mono_string_new(domain, result->Error.c_str());
            *text = *error ? nullptr : mono_string_new_len(domain, result->Text.data(), static_cast<unsigned>(result->Text.size()));
            return true;
        }

        // 即使工作线程还在读文件，移除位置后 AssetUpdate 也不会把结果交给已销毁的脚本。
        void Forget(uint64_t id) { s_Reads.erase(id); }
    }

    void RegisterCoroutineCalls()
    {
        mono_add_internal_call("Redflag.Entity::SetCoroutinesActive", reinterpret_cast<const void*>(SetActive));
        mono_add_internal_call("Redflag.TextAssetRead::Begin", reinterpret_cast<const void*>(Begin));
        mono_add_internal_call("Redflag.TextAssetRead::Poll", reinterpret_cast<const void*>(Poll));
        mono_add_internal_call("Redflag.TextAssetRead::Forget", reinterpret_cast<const void*>(Forget));
    }

    void ClearCoroutineReads() { s_Reads.clear(); }
}
