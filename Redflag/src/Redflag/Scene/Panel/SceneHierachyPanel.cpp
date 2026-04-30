#include"rfpch.h"
#include"SceneHierachyPanel.h"
#include"imgui.h"
#include"imgui_internal.h"
#include <algorithm>
#include <array>
#include<filesystem>
#include <fstream>
#include <shellapi.h>
#include<glm/gtc/type_ptr.hpp>
#include <sstream>
#include "Scene/Components.h"
#include "Redflag/Core/AssetManager.h"
#include "Redflag/Renderer/Model.h"

namespace Redflag {

    struct ScriptSourceCacheEntry
    {
        std::filesystem::path Path;
        std::filesystem::file_time_type LastWriteTime{};
        std::string Contents;
        bool Initialized = false;
        bool Found = false;
    };

    struct ScriptBuildState
    {
        bool Initialized = false;
        bool SourcesDirty = false;
        bool StatusIsError = false;
        std::string StatusMessage;
        std::chrono::steady_clock::time_point LastScanTime{};
    };

    static std::filesystem::path FindWorkspaceRoot()
    {
        std::filesystem::path currentPath = std::filesystem::current_path();

        while (!currentPath.empty())
        {
            if (std::filesystem::exists(currentPath / "SandboxProject" / "Source"))
                return currentPath;

            std::filesystem::path parentPath = currentPath.parent_path();
            if (parentPath == currentPath)
                break;

            currentPath = parentPath;
        }

        return {};
    }

    static std::array<std::filesystem::path, 2> GetScriptSourceRoots(const std::filesystem::path& workspaceRoot)
    {
        return {
            workspaceRoot / "SandboxProject" / "Source",
            workspaceRoot / "Redflag-ScriptCore" / "Source"
        };
    }

    static std::array<std::filesystem::path, 2> GetScriptAssemblyPaths(const std::filesystem::path& workspaceRoot)
    {
        const std::filesystem::path scriptsRoot = workspaceRoot / "RedflagForge" / "Resources" / "Scripts";
        return {
            scriptsRoot / "Sandbox.dll",
            scriptsRoot / "Redflag-ScriptCore.dll"
        };
    }

    static std::filesystem::path FindScriptSourcePath(const ScriptClass& scriptClass)
    {
        const std::filesystem::path workspaceRoot = FindWorkspaceRoot();
        if (workspaceRoot.empty())
            return {};

        const std::string fullClassName = scriptClass.GetFullName();
        const size_t lastDot = fullClassName.find_last_of('.');
        const std::string className = lastDot == std::string::npos ? fullClassName : fullClassName.substr(lastDot + 1);

        const auto searchRoots = GetScriptSourceRoots(workspaceRoot);

        for (const std::filesystem::path& searchRoot : searchRoots)
        {
            if (!std::filesystem::exists(searchRoot))
                continue;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(searchRoot))
            {
                if (!entry.is_regular_file())
                    continue;

                if (entry.path().filename() == className + ".cs")
                    return entry.path();
            }
        }

        return {};
    }

    static bool ReadTextFile(const std::filesystem::path& path, std::string& outContents)
    {
        std::ifstream stream(path);
        if (!stream)
            return false;

        std::stringstream buffer;
        buffer << stream.rdbuf();
        outContents = buffer.str();
        return true;
    }

    static const ScriptSourceCacheEntry& GetScriptSourceCacheEntry(const ScriptClass& scriptClass)
    {
        static std::unordered_map<std::string, ScriptSourceCacheEntry> sourceCache;

        const std::string fullClassName = scriptClass.GetFullName();
        ScriptSourceCacheEntry& cacheEntry = sourceCache[fullClassName];

        if (!cacheEntry.Initialized || (cacheEntry.Found && !std::filesystem::exists(cacheEntry.Path)))
        {
            cacheEntry.Path = FindScriptSourcePath(scriptClass);
            cacheEntry.Found = !cacheEntry.Path.empty();
            cacheEntry.Initialized = true;
            cacheEntry.Contents.clear();
            cacheEntry.LastWriteTime = {};

            if (cacheEntry.Found && std::filesystem::exists(cacheEntry.Path))
            {
                cacheEntry.LastWriteTime = std::filesystem::last_write_time(cacheEntry.Path);
                ReadTextFile(cacheEntry.Path, cacheEntry.Contents);
            }
        }
        else if (cacheEntry.Found && std::filesystem::exists(cacheEntry.Path))
        {
            const auto lastWriteTime = std::filesystem::last_write_time(cacheEntry.Path);
            if (lastWriteTime != cacheEntry.LastWriteTime || cacheEntry.Contents.empty())
            {
                cacheEntry.LastWriteTime = lastWriteTime;
                ReadTextFile(cacheEntry.Path, cacheEntry.Contents);
            }
        }
        else
        {
            cacheEntry.Found = false;
            cacheEntry.Path.clear();
            cacheEntry.Contents.clear();
        }

        return cacheEntry;
    }

    static ScriptBuildState& GetScriptBuildState()
    {
        static ScriptBuildState buildState;
        return buildState;
    }

    static bool TryGetFileWriteTime(const std::filesystem::path& path, std::filesystem::file_time_type& outWriteTime)
    {
        std::error_code errorCode;
        if (!std::filesystem::exists(path, errorCode) || errorCode)
            return false;

        outWriteTime = std::filesystem::last_write_time(path, errorCode);
        return !errorCode;
    }

    static bool TryGetLatestWriteTimeUnderRoot(const std::filesystem::path& root, const char* extension, std::filesystem::file_time_type& outWriteTime)
    {
        std::error_code errorCode;
        if (!std::filesystem::exists(root, errorCode) || errorCode)
            return false;

        bool found = false;
        for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
            it != end;
            it.increment(errorCode))
        {
            if (errorCode)
            {
                errorCode.clear();
                continue;
            }

            if (!it->is_regular_file(errorCode))
            {
                errorCode.clear();
                continue;
            }

            if (it->path().extension() != extension)
                continue;

            const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(it->path(), errorCode);
            if (errorCode)
            {
                errorCode.clear();
                continue;
            }

            if (!found || writeTime > outWriteTime)
                outWriteTime = writeTime;

            found = true;
        }

        return found;
    }

    static void RefreshScriptBuildState()
    {
        ScriptBuildState& buildState = GetScriptBuildState();
        const auto now = std::chrono::steady_clock::now();
        if (buildState.Initialized && now - buildState.LastScanTime < std::chrono::milliseconds(500))
            return;

        buildState.Initialized = true;
        buildState.LastScanTime = now;
        buildState.SourcesDirty = false;

        const std::filesystem::path workspaceRoot = FindWorkspaceRoot();
        if (workspaceRoot.empty())
            return;

        bool hasSourceFiles = false;
        std::filesystem::file_time_type latestSourceWriteTime{};
        for (const std::filesystem::path& sourceRoot : GetScriptSourceRoots(workspaceRoot))
        {
            std::filesystem::file_time_type rootWriteTime{};
            if (!TryGetLatestWriteTimeUnderRoot(sourceRoot, ".cs", rootWriteTime))
                continue;

            if (!hasSourceFiles || rootWriteTime > latestSourceWriteTime)
                latestSourceWriteTime = rootWriteTime;

            hasSourceFiles = true;
        }

        bool hasAssemblies = false;
        std::filesystem::file_time_type latestAssemblyWriteTime{};
        for (const std::filesystem::path& assemblyPath : GetScriptAssemblyPaths(workspaceRoot))
        {
            std::filesystem::file_time_type assemblyWriteTime{};
            if (!TryGetFileWriteTime(assemblyPath, assemblyWriteTime))
                continue;

            if (!hasAssemblies || assemblyWriteTime > latestAssemblyWriteTime)
                latestAssemblyWriteTime = assemblyWriteTime;

            hasAssemblies = true;
        }

        buildState.SourcesDirty = hasSourceFiles && (!hasAssemblies || latestSourceWriteTime > latestAssemblyWriteTime);
    }

    static const char* GetManagedBuildConfiguration()
    {
#if defined(RF_DEBUG)
        return "Debug";
#elif defined(RF_RELEASE)
        return "Release";
#else
        return "Dist";
#endif
    }

    static std::filesystem::path FindMSBuildPath()
    {
        std::vector<std::filesystem::path> candidatePaths;

        char programFilesX86[MAX_PATH] = {};
        const DWORD programFilesLength = GetEnvironmentVariableA("ProgramFiles(x86)", programFilesX86, MAX_PATH);
        if (programFilesLength > 0 && programFilesLength < MAX_PATH)
        {
            const std::filesystem::path basePath = programFilesX86;
            candidatePaths = {
                basePath / "Microsoft Visual Studio" / "2022" / "BuildTools" / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe",
                basePath / "Microsoft Visual Studio" / "2022" / "Community" / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe",
                basePath / "Microsoft Visual Studio" / "2022" / "Professional" / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe",
                basePath / "Microsoft Visual Studio" / "2022" / "Enterprise" / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe",
                basePath / "Microsoft Visual Studio" / "2019" / "BuildTools" / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe",
                basePath / "Microsoft Visual Studio" / "2019" / "Community" / "MSBuild" / "Current" / "Bin" / "amd64" / "MSBuild.exe"
            };
        }

        for (const std::filesystem::path& candidatePath : candidatePaths)
        {
            if (std::filesystem::exists(candidatePath))
                return candidatePath;
        }

        char resolvedPath[MAX_PATH] = {};
        const DWORD resolvedLength = SearchPathA(nullptr, "MSBuild.exe", nullptr, MAX_PATH, resolvedPath, nullptr);
        if (resolvedLength > 0 && resolvedLength < MAX_PATH)
            return resolvedPath;

        return {};
    }

    static bool RunProcessAndWait(const std::filesystem::path& executablePath, const std::string& arguments,
        const std::filesystem::path& workingDirectory, DWORD& outExitCode)
    {
        outExitCode = 1;

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(STARTUPINFOA);

        PROCESS_INFORMATION processInfo{};
        std::string commandLine = std::string("\"") + executablePath.string() + "\" " + arguments;
        std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
        commandLineBuffer.push_back('\0');

        const std::string workingDirectoryString = workingDirectory.string();
        const BOOL created = CreateProcessA(nullptr, commandLineBuffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, workingDirectoryString.c_str(), &startupInfo, &processInfo);
        if (!created)
            return false;

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        if (!GetExitCodeProcess(processInfo.hProcess, &outExitCode))
            outExitCode = 1;

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return true;
    }

    static bool OpenScriptSourceFile(const std::filesystem::path& sourcePath)
    {
        const std::string sourcePathString = sourcePath.string();
        return (INT_PTR)ShellExecuteA(nullptr, "open", sourcePathString.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
    }

    static bool RecompileScriptAssemblies(std::string& outMessage)
    {
        const std::filesystem::path workspaceRoot = FindWorkspaceRoot();
        if (workspaceRoot.empty())
        {
            outMessage = "Workspace root not found";
            return false;
        }

        if (ScriptEngine::IsRuntimeActive())
        {
            outMessage = "Stop Play mode before recompiling scripts";
            return false;
        }

        const std::filesystem::path msbuildPath = FindMSBuildPath();
        if (msbuildPath.empty())
        {
            outMessage = "MSBuild.exe not found";
            return false;
        }

        const std::filesystem::path sandboxProjectPath = workspaceRoot / "SandboxProject" / "SandboxProject.csproj";
        if (!std::filesystem::exists(sandboxProjectPath))
        {
            outMessage = "SandboxProject.csproj not found";
            return false;
        }

        if (!ScriptEngine::UnloadAssemblies())
        {
            outMessage = ScriptEngine::GetAssemblyStatusMessage();
            return false;
        }

        DWORD exitCode = 1;
        const std::string arguments = std::string("\"") + sandboxProjectPath.string() +
            "\" /t:Build /p:Configuration=" + GetManagedBuildConfiguration() +
            " /p:Platform=x64 /nologo /v:minimal";

        if (!RunProcessAndWait(msbuildPath, arguments, workspaceRoot, exitCode))
        {
            outMessage = std::string("Failed to launch MSBuild, error code ") + std::to_string(GetLastError());
            if (ScriptEngine::LoadAssemblies())
                outMessage += ", restored previous assemblies";
            return false;
        }

        if (exitCode != 0)
        {
            outMessage = std::string("Script compilation failed, MSBuild exit code ") + std::to_string(exitCode);
            if (ScriptEngine::LoadAssemblies())
                outMessage += ", restored previous assemblies";
            else
                outMessage += std::string(", failed to restore assemblies: ") + ScriptEngine::GetAssemblyStatusMessage();
            return false;
        }

        if (!ScriptEngine::LoadAssemblies())
        {
            outMessage = std::string("Script reload failed: ") + ScriptEngine::GetAssemblyStatusMessage();
            return false;
        }

        GetScriptBuildState().Initialized = false;
        outMessage = "Scripts recompiled and reloaded";
        return true;
    }

    static const char* ScriptFieldTypeToString(ScriptFieldType type)
    {
        switch (type)
        {
            case ScriptFieldType::Float:
                return "Float";
            case ScriptFieldType::Int:
                return "Int";
            case ScriptFieldType::Bool:
                return "Bool";
            case ScriptFieldType::Vector3:
                return "Vector3";
            default:
                return "Unknown";
        }
    }

	SceneHierachyPanel::SceneHierachyPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void SceneHierachyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
        m_SelectionContext = {};
	}

    void SceneHierachyPanel::OnImguiRender()
    {
        ImGui::Begin("Scene Hierachy");

        if (m_Context) {
            // ��������ʵ�岢���ƽڵ�
            auto view = m_Context->m_Registry.view<TagComponent>();
            view.each([&](auto entity, auto& tc) {
                // ����Entity���󲢻��ƽڵ�
                Entity ent{ entity, m_Context.get() };
                DrawEntityNode(ent);
                });
        }

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            m_SelectionContext = {};
        
        if (ImGui::BeginPopupContextWindow("HierarchyPopup", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
                m_Context->CreateEntity("Empty Entity");
            ImGui::EndPopup();
        }
        ImGui::End();

        ImGui::Begin("Properties");
        if (m_SelectionContext && m_Context)
        {
            DrawComponents(m_SelectionContext);
            
            /*
            if (ImGui::Button("Add Component"))
                ImGui::OpenPopup("Add Component");

            if (ImGui::BeginPopup("Add Component"))
            {
                if (ImGui::MenuItem("Camera"))
                {
                    m_SelectionContext.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Sprite Renderer"))
                {
                    m_SelectionContext.AddComponent<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
            */
        }

        ImGui::End();
    }

    void SceneHierachyPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectionContext = entity;
    }

    void SceneHierachyPanel::DrawEntityNode(Entity entity)
    {
        auto& tagComponent = entity.GetComponent<TagComponent>();

        // ���ýڵ��־
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_SelectionContext == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        // ���ݱ�־��ʵ����ƽڵ�
        bool opened = ImGui::TreeNodeEx(
            (void*)(uint64_t)(uint32_t)entity,
            flags,
            "%s",
            tagComponent.Tag.c_str()
        );

        // ���ѡ��
        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        // �ӳ�ɾ�����
        bool entityDeleted = false;

        // �Ҽ��˵� - ʹ�� BeginPopupContextItem ����ȷ��ʽ
        if (ImGui::BeginPopupContextItem())  // ����Ҫ�����жϣ������Զ������Ҽ����
        {
            if (ImGui::MenuItem("Delete Entity"))
                entityDeleted = true;  // ֻ���ñ�ǣ�������ɾ��

            ImGui::EndPopup();
        }

        // ����ڵ�򿪣�����������
        if (opened) {
            //������
            //
            //
            ImGui::TreePop();
        }

        // �ӳ�ɾ�����ڽڵ������ɺ�ִ��
        if (entityDeleted)
        {
            m_Context->DestroyEntity(entity);
            if (m_SelectionContext == entity)
                m_SelectionContext = {};
        }
    }

    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.f)
    {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0,0 });

        float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f,lineHeight };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.6f, 1.0f));        // �޺��
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.8f, 1.0f));  // ���޺��
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.5f, 1.0f));   // ���޺��
        if (ImGui::Button("x", buttonSize))
            values.x = resetValue;
        ImGui::PopStyleColor(3);


        ImGui::SameLine();
        ImGui::DragFloat("##x", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));        // �޺���
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));  // ���޺���
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.9f, 0.3f, 1.0f));   // ���޺���
        if (ImGui::Button("y", buttonSize))
            values.y = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));        // �����
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));  // �������
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.9f, 1.0f));   // �������
        if (ImGui::Button("z", buttonSize))
            values.z = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PopStyleVar();

        ImGui::Columns(1);

        ImGui::PopID();
    }

    // 编辑器模式下的字段控件：直接读写 ScriptEngine 维护的字节缓存
    // 与 DrawRuntimeScriptFieldControl 不同，这里不存在 ScriptInstance，值进入 Play 时才生效
    static void DrawEditorScriptFieldControl(const ScriptField& field, ScriptEngine::EntityScriptFieldMap& fieldCache)
    {
        const size_t typeSize = ScriptFieldTypeSize(field.Type);
        if (typeSize == 0)
        {
            ImGui::TextDisabled("%s (%s)", field.Name.c_str(), ScriptFieldTypeToString(field.Type));
            return;
        }

        // 命中缓存→显示用户填的值；未命中→显示 C# 字段初始化器（= xxx）的默认值
        // 仅"看一眼"不会写入缓存，所以不会覆盖 C# 端的初值
        auto cacheIt = fieldCache.find(field.Name);
        std::vector<uint8_t> buffer;
        if (cacheIt != fieldCache.end())
            buffer = cacheIt->second;
        else if (field.DefaultValue.size() == typeSize)
            buffer = field.DefaultValue;
        else
            buffer.assign(typeSize, 0);

        ImGui::PushID(field.Name.c_str());
        bool changed = false;

        switch (field.Type)
        {
            case ScriptFieldType::Float:
                changed = ImGui::DragFloat(field.Name.c_str(), reinterpret_cast<float*>(buffer.data()), 0.1f);
                break;
            case ScriptFieldType::Int:
                changed = ImGui::DragInt(field.Name.c_str(), reinterpret_cast<int*>(buffer.data()));
                break;
            case ScriptFieldType::Bool:
                changed = ImGui::Checkbox(field.Name.c_str(), reinterpret_cast<bool*>(buffer.data()));
                break;
            case ScriptFieldType::Vector3:
            {
                glm::vec3* vecPtr = reinterpret_cast<glm::vec3*>(buffer.data());
                glm::vec3 previousValue = *vecPtr;
                DrawVec3Control(field.Name, *vecPtr);
                changed = (previousValue != *vecPtr);
                break;
            }
            default:
                ImGui::TextDisabled("%s", field.Name.c_str());
                break;
        }

        ImGui::PopID();

        // 仅在用户实际修改时才写入缓存，避免无意覆盖 C# 默认值
        if (changed)
            fieldCache[field.Name] = std::move(buffer);
    }

    static void DrawRuntimeScriptFieldControl(const ScriptField& field, const Ref<ScriptInstance>& scriptInstance)
    {
        ImGui::PushID(field.Name.c_str());

        switch (field.Type)
        {
            case ScriptFieldType::Float:
            {
                float value = 0.0f;
                if (!scriptInstance->GetFieldValue(field, &value))
                {
                    ImGui::TextDisabled("%s", field.Name.c_str());
                    break;
                }

                if (ImGui::DragFloat(field.Name.c_str(), &value, 0.1f))
                    scriptInstance->SetFieldValue(field, &value);
                break;
            }
            case ScriptFieldType::Int:
            {
                int value = 0;
                if (!scriptInstance->GetFieldValue(field, &value))
                {
                    ImGui::TextDisabled("%s", field.Name.c_str());
                    break;
                }

                if (ImGui::DragInt(field.Name.c_str(), &value))
                    scriptInstance->SetFieldValue(field, &value);
                break;
            }
            case ScriptFieldType::Bool:
            {
                bool value = false;
                if (!scriptInstance->GetFieldValue(field, &value))
                {
                    ImGui::TextDisabled("%s", field.Name.c_str());
                    break;
                }

                if (ImGui::Checkbox(field.Name.c_str(), &value))
                    scriptInstance->SetFieldValue(field, &value);
                break;
            }
            case ScriptFieldType::Vector3:
            {
                glm::vec3 value(0.0f);
                if (!scriptInstance->GetFieldValue(field, &value))
                {
                    ImGui::TextDisabled("%s", field.Name.c_str());
                    break;
                }

                glm::vec3 previousValue = value;
                DrawVec3Control(field.Name, value);
                if (value.x != previousValue.x || value.y != previousValue.y || value.z != previousValue.z)
                    scriptInstance->SetFieldValue(field, &value);
                break;
            }
            default:
                ImGui::TextDisabled("%s (%s)", field.Name.c_str(), ScriptFieldTypeToString(field.Type));
                break;
        }

        ImGui::PopID();
    }

    template<typename T, typename UIFunction>
    static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

        if (entity.HasComponent<T>())
        {
            auto& component = entity.GetComponent<T>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
            
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4,4 });
            float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
            ImGui::Separator();
            bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
            ImGui::PopStyleVar();
            ImGui::SameLine(contentRegionAvailable.x-lineHeight*0.5f);
            if (ImGui::Button("+", ImVec2{ lineHeight,lineHeight }))
            {
                ImGui::OpenPopup("ComponentSettings");
            }

            bool removeComponent = false;
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (ImGui::MenuItem("Remove component"))
                    removeComponent = true;

                ImGui::EndPopup();
            }

            if (open)
            {
                auto& src = entity.GetComponent<T>();
                uiFunction(component);
                ImGui::TreePop();
            }

            if (removeComponent)
                entity.RemoveComponent<T>();
        }
    }

    void SceneHierachyPanel::DrawComponents(Entity entity)
    {
        if (!entity) return;

       
        const ImGuiTreeNodeFlags tagTreeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        // Tag��� 
        if (entity.HasComponent<TagComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy_s(buffer, sizeof(buffer), tag.c_str());
            if (ImGui::InputText("##ReName", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(-1);
        if (ImGui::Button("Add Comp"))
            ImGui::OpenPopup("Add Comp");

        if (ImGui::BeginPopup("Add Comp"))
        {
            if (!m_SelectionContext.HasComponent<CameraComponent>())
            {
                if (ImGui::MenuItem("Camera"))
                {
                    m_SelectionContext.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<ScriptComponent>())
            {
                if (ImGui::MenuItem("Script"))
                {
                    m_SelectionContext.AddComponent<ScriptComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            
            if (!m_SelectionContext.HasComponent<SpriteRendererComponent>())
            {
                if (ImGui::MenuItem("Sprite Renderer"))
                {
                    m_SelectionContext.AddComponent<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<CircleRendererComponent>())
            {
                if (ImGui::MenuItem("Circle Renderer"))
                {
                    m_SelectionContext.AddComponent<CircleRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            
            if (!m_SelectionContext.HasComponent<RigidBody2DComponent>())
            {
                if (ImGui::MenuItem("Rigidbody 2D"))
                {
                    m_SelectionContext.AddComponent<RigidBody2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<BoxCollider2DComponent>())
            {
                if (ImGui::MenuItem("Box Collider 2D"))
                {
                    m_SelectionContext.AddComponent<BoxCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<CircleCollider2DComponent>())
            {
                if (ImGui::MenuItem("Circle Collider 2D"))
                {
                    m_SelectionContext.AddComponent<CircleCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<MeshRendererComponent>())
            {
                if (ImGui::MenuItem("Mesh Renderer"))
                {
                    auto& mr = m_SelectionContext.AddComponent<MeshRendererComponent>();
                    mr.MeshData = Mesh::CreateCube();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<ModelRendererComponent>())
            {
                if (ImGui::MenuItem("Model Renderer"))
                {
                    m_SelectionContext.AddComponent<ModelRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Separator();

            if (!m_SelectionContext.HasComponent<DirectionalLightComponent>())
            {
                if (ImGui::MenuItem("Directional Light"))
                {
                    m_SelectionContext.AddComponent<DirectionalLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<PointLightComponent>())
            {
                if (ImGui::MenuItem("Point Light"))
                {
                    m_SelectionContext.AddComponent<PointLightComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::Separator();

            if (!m_SelectionContext.HasComponent<ParticleEmitterComponent>())
            {
                if (ImGui::MenuItem("Particle Emitter 2D"))
                {
                    m_SelectionContext.AddComponent<ParticleEmitterComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<TextureAnimationComponent>())
            {
                if (ImGui::MenuItem("Texture Animation"))
                {
                    m_SelectionContext.AddComponent<TextureAnimationComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopItemWidth();


        // Transform���
        DrawComponent<TransformComponent>("Transform", entity, [](auto& component)
            {
                DrawVec3Control("Translation", component.Translation);
                glm::vec3 rotation = glm::degrees(component.Rotation);
                DrawVec3Control("Rotation", rotation);
                component.Rotation = glm::radians(rotation);
                DrawVec3Control("Scale", component.Scale, 1.0f);
            });

        // Camera���
        DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
            {
                auto& camera = component.Camera;

                ImGui::Checkbox("Primary", &component.Primary);

                const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];

                if (ImGui::BeginCombo("CamType", currentProjectionTypeString))
                {
                    for (int i = 0; i < 2; i++)
                    {
                        bool isSelected = (currentProjectionTypeString == projectionTypeStrings[i]);
                        if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
                        {
                            currentProjectionTypeString = projectionTypeStrings[i];
                            camera.SetProjectionType((SceneCamera::ProjectionType)i);
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
                {
                    float verticalFov = glm::degrees(camera.GetPerspectiveFOV());
                    if (ImGui::DragFloat("Fov", &verticalFov))
                        camera.SetPerspectiveFOV(glm::radians(verticalFov));

                    float perspectiveNear = camera.GetPerspectiveNearClip();
                    if (ImGui::DragFloat("Near", &perspectiveNear))
                        camera.SetPerspectiveNearClip(perspectiveNear);

                    float perspectiveFar = camera.GetPerspectiveFarClip();
                    if (ImGui::DragFloat("Far", &perspectiveFar))
                        camera.SetPerspectiveFarClip(perspectiveFar);
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                {
                    float orthoSize = camera.GetOrthographicSize();
                    if (ImGui::DragFloat("Size", &orthoSize))
                        camera.SetOrthographicSize(orthoSize);

                    float orthoNear = camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("Near", &orthoNear))
                        camera.SetOrthographicNearClip(orthoNear);

                    float orthoFar = camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("Far", &orthoFar))
                        camera.SetOrthographicFarClip(orthoFar);

                    ImGui::Checkbox("Fixed AspectRatio", &component.FixedAspectRatio);
                }

            });

        // Script 组件面板
        DrawComponent<ScriptComponent>("Script Component", entity, [entity](auto& component)
            {
                const auto& entityClasses = ScriptEngine::GetEntityClasses();

                std::vector<std::string> classNames;
                classNames.reserve(entityClasses.size());
                for (const auto& [className, scriptClass] : entityClasses)
                    classNames.push_back(className);

                std::sort(classNames.begin(), classNames.end());

                const bool scriptClassExsists = entityClasses.find(component.ClassName) != entityClasses.end();
                const char* previewValue = component.ClassName.empty() ? "<None>" : component.ClassName.c_str();
               
                if (!scriptClassExsists)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.3f, 1.0f));

                if (ImGui::BeginCombo("Class", previewValue))
                {
                    const bool noneSelected = component.ClassName.empty();
                    if (ImGui::Selectable("<None>", noneSelected))
                        component.ClassName.clear();

                    if (noneSelected)
                        ImGui::SetItemDefaultFocus();

                    for (const std::string& className : classNames)
                    {
                        const bool isSelected = component.ClassName == className;
                        if (ImGui::Selectable(className.c_str(), isSelected))
                            component.ClassName = className;

                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
                }

                if (!scriptClassExsists)
                    ImGui::PopStyleColor();

				const auto scriptClassIt = entityClasses.find(component.ClassName);
				const bool hasValidScriptClass = scriptClassIt != entityClasses.end();

                if (!component.ClassName.empty() && !hasValidScriptClass)
                    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.3f, 1.0f), "Missing script class: %s", component.ClassName.c_str());

                if (!hasValidScriptClass)
                    return;

                const Ref<ScriptClass>& scriptClass = scriptClassIt->second;
                const auto& fields = scriptClass->GetFields();
                Ref<ScriptInstance> scriptInstance = ScriptEngine::GetEntityScriptInstance(entity.GetUUID());

                ImGui::Spacing();
                ImGui::Separator();
                if (scriptInstance)
                    ImGui::TextDisabled("Runtime Fields");

                if (fields.empty())
                {
                    ImGui::TextDisabled("No supported public fields");
                }
                else if (!scriptInstance)
				{
					// 编辑器模式：通过字段缓存编辑，进入 Play 时这些值会被写入 ScriptInstance
					auto& fieldCache = ScriptEngine::GetScriptFieldCache(entity.GetUUID());
					for (const ScriptField& field : fields)
						DrawEditorScriptFieldControl(field, fieldCache);
				}
                else
                {
                    for (const ScriptField& field : fields)
                        DrawRuntimeScriptFieldControl(field, scriptInstance);
                }

                ImGui::Spacing();
                ImGui::Separator();

                ScriptBuildState& buildState = GetScriptBuildState();
                RefreshScriptBuildState();

                const ImVec4 attentionRed = ImVec4(0.95f, 0.16f, 0.20f, 1.0f);
                const ImVec4 successGreen = ImVec4(0.12f, 0.82f, 0.28f, 1.0f);

                if (buildState.SourcesDirty)
                {
                    ImGui::TextColored(attentionRed, "%s", "Scripts changed. Click to recompile.");

                    const bool canRecompileScripts = !ScriptEngine::IsRuntimeActive();
                    if (!canRecompileScripts)
                        ImGui::BeginDisabled();

                    if (ImGui::Button("Recompile Scripts"))
                    {
                        std::string statusMessage;
                        const bool recompileSucceeded = RecompileScriptAssemblies(statusMessage);
                        buildState.StatusMessage = statusMessage;
                        buildState.StatusIsError = !recompileSucceeded;
                        buildState.Initialized = false;
                    }

                    if (!canRecompileScripts)
                        ImGui::EndDisabled();

                    if (!canRecompileScripts)
                        ImGui::TextColored(attentionRed, "%s", "Stop Play mode before recompiling");
                }

                if (!buildState.StatusMessage.empty())
                {
                    const ImVec4 statusColor = buildState.StatusIsError
                        ? attentionRed
                        : successGreen;
                    ImGui::TextColored(statusColor, "%s", buildState.StatusMessage.c_str());
                }

                const ScriptSourceCacheEntry& sourceEntry = GetScriptSourceCacheEntry(*scriptClass);
                if (!sourceEntry.Found)
                {
                    ImGui::TextDisabled("Source file not found");
                    return;
                }

                if (ImGui::Button("Open File"))
                {
                    if (!OpenScriptSourceFile(sourceEntry.Path))
                    {
                        buildState.StatusMessage = std::string("Failed to open script file: ") + sourceEntry.Path.string();
                        buildState.StatusIsError = true;
                    }
                }

                ImGui::TextWrapped("%s", sourceEntry.Path.string().c_str());
                ImGui::BeginChild("ScriptSourceView", ImVec2(0.0f, 260.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(sourceEntry.Contents.c_str());
                ImGui::EndChild();
            });


        // SpriteRender
        DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component)
            {
                // ��ɫ�༭
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

                // ƽ������
                ImGui::DragFloat("Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.f);

                // ========== ������ק���� ==========
                ImGui::Separator();
                ImGui::Text("Texture");

                // ��ʾ��ǰ����Ԥ��
                if (component.Texture && component.Texture->GetRendererID())
                {
                    // ����Ԥ��
                    ImGui::Image(
                        (void*)(intptr_t)component.Texture->GetRendererID(),
                        ImVec2(64, 64)
                    );
                }
                else
                {
                    // ������ռλ
                    ImGui::Button("No Texture", ImVec2(64, 64));
                }

                // ��קĿ��
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                    {
                        const char* path = (const char*)payload->Data;
                        if (path)
                        {
                            component.Texture = Texture2D::Create(path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // ���������ť
                if (component.Texture)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Texture"))
                    {
                        component.Texture = nullptr;
                    }
                }
                // =================================
            });


        //CircleRendererComponent
        DrawComponent<CircleRendererComponent>("Circle Renderer", entity, [](auto& component)
            {
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
                ImGui::DragFloat("Thickness", &component.Thickness, 0.025f, 0.f, 1.f);
                ImGui::DragFloat("Fade", &component.Fade, 0.0025f, 2.f, 1.f);
            });

        // Rigidbody2D ���
        DrawComponent<RigidBody2DComponent>("Rigidbody 2D", entity, [](auto& component)
            {
                // ��������������
                const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
                const char* currentType = bodyTypeStrings[(int)component.Type];

                if (ImGui::BeginCombo("Body Type", currentType))
                {
                    for (int i = 0; i < 3; i++)
                    {
                        bool isSelected = (currentType == bodyTypeStrings[i]);
                        if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
                        {
                            component.Type = (RigidBody2DComponent::BodyType)i;
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // �̶���ת
                ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
            });

        // BoxCollider2D ���
        DrawComponent<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component)
            {
                // ƫ����
                ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
                // ��ײ�гߴ�
                ImGui::DragFloat2("Size", glm::value_ptr(component.Size));
                // �ܶ�
                ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
                // Ħ����
                ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
                // ����
                ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
                // ������ֵ
                ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
            });

        // CircleCollider2D 属性面板
        DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
            {
                // 碰撞圆心相对实体原点的偏移
                ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
                // 碰撞圆的半径
                ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.001f, FLT_MAX);
                // 密度
                ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
                // 摩擦系数
                ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
                // 恢复系数（弹性）
                ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
                // 恢复生效的最低碰撞速度阈值
                ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
            });

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, [](auto& component)
            {
                // 如果还没有 Material，自动创建一个默认 PBR 材质
                if (!component.Mat)
                {
                    component.Mat = Material::Create(component.Color);
                    if (component.Texture)
                        component.Mat->BaseColorMap = component.Texture;
                }
                Ref<Material>& mat = component.Mat;

                // ---- 基础颜色 ----
                if (ImGui::ColorEdit4("Base Color", glm::value_ptr(mat->BaseColor)))
                    component.Color = mat->BaseColor;  // 保持 mr.Color 与 mat->BaseColor 同步

                // ---- PBR 标量参数 ----
                ImGui::SliderFloat("Metallic",  &mat->Metallic,  0.0f, 1.0f);
                ImGui::SliderFloat("Roughness", &mat->Roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("AO",        &mat->AO,        0.0f, 1.0f);

                // ---- 贴图槽位（统一的 helper：预览 + 拖拽 + 清除）----
                auto TextureSlotUI = [](const char* label, Ref<Texture2D>& tex)
                {
                    ImGui::Separator();
                    ImGui::Text("%s", label);
                    if (tex && tex->GetRendererID())
                        ImGui::Image((void*)(intptr_t)tex->GetRendererID(), ImVec2(48, 48));
                    else
                        ImGui::Button("None##btn", ImVec2(48, 48));

                    // 拖拽接受
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                        {
                            const char* path = (const char*)payload->Data;
                            if (path)
                                tex = Texture2D::Create(path);
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (tex)
                    {
                        ImGui::SameLine();
                        // 用 PushID 避免多个 Clear 按钮 ID 冲突
                        ImGui::PushID(label);
                        if (ImGui::Button("Clear"))
                            tex = nullptr;
                        ImGui::PopID();
                    }
                };

                TextureSlotUI("BaseColor Map",          mat->BaseColorMap);
                TextureSlotUI("Normal Map",             mat->NormalMap);
                TextureSlotUI("MetallicRoughness Map",  mat->MetallicRoughnessMap);
                TextureSlotUI("AO Map",                 mat->AOMap);
            });

        // Model Renderer
        DrawComponent<ModelRendererComponent>("Model Renderer", entity, [](auto& component)
            {
                // 文件路径（只读显示）
                ImGui::Text("File: %s", component.FilePath.empty() ? "(none)" : component.FilePath.c_str());

                // 拖拽接受区：从 Content Browser 拖入模型文件
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL"))
                    {
                        const char* path = (const char*)payload->Data;
                        if (path)
                        {
                            component.FilePath    = path;
                            component.ModelAsset  = AssetManager::GetOrLoad<Model>(component.FilePath);
                            component.OverrideMaterials.clear(); // 重新加载模型时清除覆盖材质
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 备用颜色
                ImGui::ColorEdit4("Fallback Color", glm::value_ptr(component.Color));

                // 卸载按钮
                if (component.ModelAsset)
                {
                    if (ImGui::Button("Unload"))
                    {
                        component.ModelAsset.reset();
                    }
                }

                // ---- 模型自带材质信息（只读预览）----
                if (component.ModelAsset)
                {
                    const auto& subMeshes = component.ModelAsset->GetSubMeshes();
                    if (!subMeshes.empty() && ImGui::TreeNode("Imported Materials"))
                    {
                        for (size_t i = 0; i < subMeshes.size(); i++)
                        {
                            const auto& sub = subMeshes[i];
                            ImGui::PushID((int)i);
                            if (ImGui::TreeNode("SubMesh", "SubMesh %d", (int)i))
                            {
                                if (sub.Mat)
                                {
                                    ImGui::Text("BaseColor: (%.2f, %.2f, %.2f, %.2f)",
                                        sub.Mat->BaseColor.r, sub.Mat->BaseColor.g,
                                        sub.Mat->BaseColor.b, sub.Mat->BaseColor.a);
                                    ImGui::Text("Metallic: %.2f  Roughness: %.2f  AO: %.2f",
                                        sub.Mat->Metallic, sub.Mat->Roughness, sub.Mat->AO);
                                    ImGui::Text("Maps: %s%s%s%s",
                                        sub.Mat->HasBaseColorMap()         ? "[Base] " : "",
                                        sub.Mat->HasNormalMap()            ? "[Normal] " : "",
                                        sub.Mat->HasMetallicRoughnessMap() ? "[MR] " : "",
                                        sub.Mat->HasAOMap()                ? "[AO]" : "");
                                }
                                else
                                {
                                    ImGui::TextDisabled("No material");
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                }

                // ---- 覆盖材质（可选，per-submesh，优先级高于模型自带材质）----
                ImGui::Separator();
                bool hasOverride = !component.OverrideMaterials.empty();
                if (ImGui::Checkbox("Override Materials", &hasOverride))
                {
                    if (hasOverride && component.OverrideMaterials.empty() && component.ModelAsset)
                    {
                        // 从每个子网格克隆材质
                        const auto& subMeshes = component.ModelAsset->GetSubMeshes();
                        component.OverrideMaterials.resize(subMeshes.size());
                        for (size_t i = 0; i < subMeshes.size(); i++)
                        {
                            component.OverrideMaterials[i] = subMeshes[i].Mat
                                ? subMeshes[i].Mat->Clone()
                                : Material::Create(component.Color);
                        }
                    }
                    else if (!hasOverride)
                    {
                        component.OverrideMaterials.clear();
                    }
                }

                if (!component.OverrideMaterials.empty())
                {
                    auto TextureSlotUI = [](const char* label, Ref<Texture2D>& tex)
                    {
                        ImGui::Separator();
                        ImGui::Text("%s", label);
                        if (tex && tex->GetRendererID())
                            ImGui::Image((void*)(intptr_t)tex->GetRendererID(), ImVec2(48, 48));
                        else
                            ImGui::Button("None##btn", ImVec2(48, 48));

                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                            {
                                const char* path = (const char*)payload->Data;
                                if (path)
                                    tex = Texture2D::Create(path);
                            }
                            ImGui::EndDragDropTarget();
                        }
                        if (tex)
                        {
                            ImGui::SameLine();
                            ImGui::PushID(label);
                            if (ImGui::Button("Clear"))
                                tex = nullptr;
                            ImGui::PopID();
                        }
                    };

                    for (size_t i = 0; i < component.OverrideMaterials.size(); i++)
                    {
                        Ref<Material>& mat = component.OverrideMaterials[i];
                        if (!mat) continue;

                        ImGui::PushID((int)i);
                        if (ImGui::TreeNode("SubMeshMat", "SubMesh %d Material", (int)i))
                        {
                            ImGui::ColorEdit4("Base Color", glm::value_ptr(mat->BaseColor));
                            ImGui::SliderFloat("Metallic",  &mat->Metallic,  0.0f, 1.0f);
                            ImGui::SliderFloat("Roughness", &mat->Roughness, 0.0f, 1.0f);
                            ImGui::SliderFloat("AO",        &mat->AO,        0.0f, 1.0f);

                            TextureSlotUI("BaseColor Map",          mat->BaseColorMap);
                            TextureSlotUI("Normal Map",             mat->NormalMap);
                            TextureSlotUI("MetallicRoughness Map",  mat->MetallicRoughnessMap);
                            TextureSlotUI("AO Map",                 mat->AOMap);

                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }
            });

        // Directional Light 属性面板
        DrawComponent<DirectionalLightComponent>("Directional Light", entity, [](auto& component)
            {
                // 光线方向（世界空间），shader 中取反得到指向光源的向量
                ImGui::DragFloat3("Direction", glm::value_ptr(component.Direction), 0.01f);
                // 光源颜色
                ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
                // 辐照度缩放
                ImGui::DragFloat("Intensity", &component.Intensity, 0.05f, 0.0f, 100.0f);
                // 是否投射阴影
                ImGui::Checkbox("Cast Shadow", &component.CastShadow);
            });

        // Point Light 属性面板
        DrawComponent<PointLightComponent>("Point Light", entity, [](auto& component)
            {
                // 光源颜色
                ImGui::ColorEdit3("Color", glm::value_ptr(component.Color));
                // 辐照度缩放
                ImGui::DragFloat("Intensity", &component.Intensity, 0.05f, 0.0f, 100.0f);
                // 影响半径
                ImGui::DragFloat("Radius", &component.Radius, 0.1f, 0.01f, 1000.0f);
                // 是否投射阴影（点光用立方体 Shadow Map，开销较大）
                ImGui::Checkbox("Cast Shadow", &component.CastShadow);
            });

        // Particle Emitter 2D 属性面板
        DrawComponent<ParticleEmitterComponent>("Particle Emitter 2D", entity, [](auto& component)
            {
                ImGui::Checkbox("Active", &component.Active);

                if (ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::ColorEdit4("Color Begin", glm::value_ptr(component.ColorBegin));
                    ImGui::ColorEdit4("Color End",   glm::value_ptr(component.ColorEnd));
                }

                if (ImGui::CollapsingHeader("Size", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat("Size Begin",     &component.SizeBegin,     0.01f, 0.001f, 10.0f);
                    ImGui::DragFloat("Size End",       &component.SizeEnd,       0.01f, 0.001f, 10.0f);
                    ImGui::DragFloat("Size Variation", &component.SizeVariation, 0.01f, 0.0f,   5.0f);
                }

                if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat2("Velocity",          glm::value_ptr(component.Velocity),          0.1f);
                    ImGui::DragFloat2("Velocity Variation", glm::value_ptr(component.VelocityVariation), 0.1f);
                }

                ImGui::DragFloat("Life Time",      &component.LifeTime,     0.1f, 0.1f, 30.0f);
                ImGui::DragFloat("Emission Rate",  &component.EmissionRate, 1.0f, 0.0f, 10000.0f);

                int maxP = (int)component.MaxParticles;
                if (ImGui::DragInt("Max Particles", &maxP, 10.0f, 10, 100000))
                    component.MaxParticles = (uint32_t)maxP;
            });

        // Texture Animation 属性面板
        DrawComponent<TextureAnimationComponent>("Texture Animation", entity, [](auto& component)
            {
                ImGui::DragInt("Columns",    &component.Columns,        1.0f, 1, 64);
                ImGui::DragInt("Rows",       &component.Rows,           1.0f, 1, 64);
                ImGui::DragInt("Frame Count", &component.FrameCount,    1.0f, 1, 4096);
                ImGui::DragFloat("Speed (FPS)", &component.AnimationSpeed, 0.5f, 0.0f, 120.0f);
                ImGui::Checkbox("Loop", &component.Loop);

                ImGui::Separator();
                ImGui::Text("Runtime: Frame %d / %d", component.CurrentFrame, component.FrameCount);
            });
    }
}