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
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/AI/EnvironmentSense.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/RendererAPI.h"
#include "Redflag/Utils/PlatformUtils.h"

namespace Redflag {

    static void DrawHeightMap(Material& material)
    {
        if (ImGui::Button("选择高度贴图"))
        {
            const std::string image = FileDialogs::OpenFile("Height image\0*.png;*.jpg;*.jpeg;*.tga\0");
            if (!image.empty())
                material.HeightMap = Texture2D::Create(std::filesystem::proximate(std::filesystem::u8path(image)).generic_u8string());
        }
        if (!material.HeightMap) return;
        ImGui::SameLine();
        if (ImGui::SmallButton("移除高度图")) { material.HeightMap.reset(); return; }

        int mode = int(material.HeightMode);
        const char* names[] = { "关闭", "凹凸", "视差遮蔽", "顶点置换" };
        if (ImGui::Combo("高度贴图效果", &mode, names, 4)) material.HeightMode = HeightMapping(mode);
        ImGui::DragFloat("黑白高度差（模型米）", &material.HeightScale, .0001f, 0.f, 10.f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Vulkan 不透明材质使用高度图的红通道，白色最高。顶点置换需要足够密的网格。");
        if (material.HeightMode == HeightMapping::Displacement)
        {
            ImGui::SliderFloat("原表面的灰度", &material.HeightReference, 0.f, 1.f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("等于这个灰度的顶点留在原位。更亮的向外凸，更暗的向内凹。");
        }
    }

    static void DrawModelLODs(ModelRendererComponent& model)
    {
        ImGui::Text("当前网格  LOD %d", int(model.ActiveLOD));
        for (size_t i = 0; i < model.LODs.size(); ++i)
        {
            auto& lod = model.LODs[i];
            ImGui::PushID(int(i));
            ImGui::Text("LOD %d  %s", int(i + 1), std::filesystem::path(lod.FilePath).filename().u8string().c_str());
            const float low = i + 1 < model.LODs.size() ? model.LODs[i + 1].ScreenHeight : 0.f;
            const float high = i ? model.LODs[i - 1].ScreenHeight : 1.f;
            float percent = lod.ScreenHeight * 100.f;
            if (ImGui::SliderFloat("视口高度", &percent, low * 100.f + .01f, high * 100.f - .01f, "%.2f%%", ImGuiSliderFlags_AlwaysClamp))
                lod.ScreenHeight = percent / 100.f;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("模型缩小到视口高度的这个比例附近时，改用这份网格。");
            const bool remove = ImGui::SmallButton("移除此级");
            ImGui::PopID();
            if (remove)
            {
                model.LODs.erase(model.LODs.begin() + i);
                model.ActiveLOD = 0;
                break;
            }
        }

        if (ImGui::Button("添加简化模型…"))
        {
            const auto path = FileDialogs::OpenFile("3D Model (*.obj;*.fbx;*.gltf;*.glb;*.ply)\0*.obj;*.fbx;*.gltf;*.glb;*.ply\0");
            if (path.empty()) return;
            ModelLOD lod;
            lod.ModelID = AssetManager::Add<Model>(path);
            lod.ModelAsset = AssetManager::Load<Model>(lod.ModelID);
            if (!lod.ModelAsset) return;
            lod.FilePath = std::filesystem::proximate(path).generic_string();
            lod.ScreenHeight = model.LODs.empty() ? .25f : .5f * model.LODs.back().ScreenHeight;
            model.LODs.push_back(std::move(lod));
        }
    }

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
            case ScriptFieldType::Entity:
                return "Entity";
            default:
                return "Unknown";
        }
    }

    static constexpr const char* s_SceneEntityPayloadType = "SCENE_ENTITY";

    static uint64_t ReadEntityIDFromBuffer(const std::vector<uint8_t>& buffer)
    {
        uint64_t entityID = 0;
        if (buffer.size() >= sizeof(entityID))
            memcpy(&entityID, buffer.data(), sizeof(entityID));
        return entityID;
    }

    static void WriteEntityIDToBuffer(std::vector<uint8_t>& buffer, uint64_t entityID)
    {
        buffer.assign(sizeof(entityID), 0);
        memcpy(buffer.data(), &entityID, sizeof(entityID));
    }

    static Ref<Texture2D> GetEntityIconTexture()
    {
        // 此处的图标用 OpenGL 纹理编号显示，其他后端沿用下方的文字按钮。
        if (RendererAPI::GetAPI() != RendererAPI::API::OpenGL)
            return nullptr;
        static Ref<Texture2D> iconTexture;
        static bool loadAttempted = false;

        if (!loadAttempted)
        {
            loadAttempted = true;
            const std::filesystem::path iconPath = "assets/icons/Entity.png";
            if (std::filesystem::exists(iconPath))
                iconTexture = Texture2D::Create(iconPath.string());
        }

        return iconTexture;
    }

    static bool AcceptSceneEntityPayload(uint64_t& entityID)
    {
        bool changed = false;

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(s_SceneEntityPayloadType))
            {
                if (payload->DataSize == sizeof(uint64_t))
                {
                    uint64_t droppedEntityID = 0;
                    memcpy(&droppedEntityID, payload->Data, sizeof(droppedEntityID));
                    if (entityID != droppedEntityID)
                    {
                        entityID = droppedEntityID;
                        changed = true;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        return changed;
    }

    static std::string GetEntityReferenceLabel(Scene* scene, uint64_t entityID)
    {
        if (entityID == 0)
            return "None";

        if (scene)
        {
            Entity entity = scene->GetEntityByUUID(UUID(entityID));
            if (entity)
                return entity.GetName();
        }

        return std::string("Missing (") + std::to_string(entityID) + ")";
    }

    static void PushScriptFieldFillStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.86f, 0.18f, 0.18f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.92f, 0.22f, 0.22f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.78f, 0.14f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.18f, 0.18f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.22f, 0.22f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78f, 0.14f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.86f, 0.18f, 0.18f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.92f, 0.22f, 0.22f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.90f, 0.90f, 1.0f));
    }

    static void PopScriptFieldFillStyle()
    {
        ImGui::PopStyleColor(9);
    }

    static void DrawReadOnlyTextControl(const std::string& label, const std::string& value, float columnWidth = 100.0f)
    {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::NextColumn();

        ImGui::TextDisabled("%s", value.c_str());

        ImGui::Columns(1);
        ImGui::PopID();
    }

    static bool DrawEntityReferenceControl(const std::string& label, Scene* scene, uint64_t& entityID, float columnWidth = 100.0f)
    {
        bool changed = false;

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::TextUnformatted(label.c_str());
        ImGui::NextColumn();

        const float clearButtonWidth = 28.0f;
        const float iconSize = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const bool hasEntityReference = entityID != 0;
        const float iconAndSpacingWidth = hasEntityReference ? iconSize + spacing : 0.0f;
        const float buttonWidth = std::max(80.0f, availableWidth - iconAndSpacingWidth - clearButtonWidth - spacing);

        if (hasEntityReference)
        {
            Ref<Texture2D> entityIcon = GetEntityIconTexture();
            if (entityIcon && entityIcon->GetRendererID())
                ImGui::Image((void*)(intptr_t)entityIcon->GetRendererID(), ImVec2(iconSize, iconSize));
            else
                ImGui::Button("E", ImVec2(iconSize, iconSize));

            changed |= AcceptSceneEntityPayload(entityID);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Entity reference");

            ImGui::SameLine();
        }

        std::string buttonLabel = GetEntityReferenceLabel(scene, entityID) + "##EntityReference";
    PushScriptFieldFillStyle();
        ImGui::Button(buttonLabel.c_str(), ImVec2(buttonWidth, 0.0f));
    PopScriptFieldFillStyle();
        changed |= AcceptSceneEntityPayload(entityID);

        ImGui::SameLine();
        const bool clearDisabled = entityID == 0;
        if (clearDisabled)
            ImGui::BeginDisabled();

        if (ImGui::Button("X", ImVec2(clearButtonWidth, 0.0f)) && !clearDisabled)
        {
            entityID = 0;
            changed = true;
        }

        if (clearDisabled)
            ImGui::EndDisabled();

        ImGui::Columns(1);

        return changed;
    }

	SceneHierachyPanel::SceneHierachyPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void SceneHierachyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
        m_SelectionContext = {};
        m_EntityToDelete = {};
	}

    void SceneHierachyPanel::OnImguiRender(const char* hierarchy, const char* inspector)
    {
        ImGui::Begin(hierarchy);

        if (m_Context) {
            // 遍历所有实体并绘制节点
            auto view = m_Context->m_Registry.view<TagComponent>();
            view.each([&](auto entity, auto& tc) {
                // 创建Entity对象并绘制节点
                Entity ent{ entity, m_Context.get() };
                DrawEntityNode(ent);
                });

            if (m_EntityToDelete)
            {
                if (m_SelectionContext == m_EntityToDelete)
                    m_SelectionContext = {};

                m_Context->DestroyEntity(m_EntityToDelete);
                m_EntityToDelete = {};
            }
        }

        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
            m_SelectionContext = {};
        
        if (ImGui::BeginPopupContextWindow("HierarchyPopup", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("创建空对象"))
                m_SelectionContext = m_Context->CreateEntity("空对象");
            ImGui::EndPopup();
        }
        ImGui::End();

        ImGui::Begin(inspector);
        if (m_SelectionContext && m_Context)
        {
            ImGui::BeginChild("PropertiesContent", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            DrawComponents(m_SelectionContext);
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
            ImGui::EndChild();
        }
        else
        {
            ImGui::TextWrapped("先在左侧层级里选一个对象，就能在这里改它的位置、外观或灯光。");
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

        // 设置节点标志
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_SelectionContext == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        // 根据标志和实体绘制节点
        bool opened = ImGui::TreeNodeEx(
            (void*)(uint64_t)(uint32_t)entity,
            flags,
            "%s",
            tagComponent.Tag.c_str()
        );

        // Select on release so dragging an entity into the Properties panel does not switch the inspected entity.
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_SelectionContext = entity;
        }

        if (ImGui::BeginDragDropSource())
        {
            uint64_t entityID = entity.GetUUID();
            ImGui::SetDragDropPayload(s_SceneEntityPayloadType, &entityID, sizeof(entityID));
            ImGui::TextUnformatted(tagComponent.Tag.c_str());
            ImGui::EndDragDropSource();
        }

        // 延迟删除标记
        bool entityDeleted = false;

        // 右键菜单 - 使用 BeginPopupContextItem 的正确方式
        if (ImGui::BeginPopupContextItem())  // 不需要条件判断，它会自动处理右键点击
        {
            if (ImGui::MenuItem("删除对象"))
                entityDeleted = true;  // 只设置标记，不立即删除

            ImGui::EndPopup();
        }

        // 如果节点打开，添加子内容
        if (opened) {
            //子内容
            //
            //
            ImGui::TreePop();
        }

        // 延迟删除：在节点绘制完成后执行
        if (entityDeleted)
        {
			m_EntityToDelete = entity;
        }
    }

    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.f)
    {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, std::min(columnWidth, ImGui::GetWindowWidth() * .25f));
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        const char* axes[] = { "X", "Y", "Z" };
        const ImVec4 colors[] = { { .46f,.18f,.20f,1 }, { .22f,.38f,.29f,1 }, { .22f,.30f,.44f,1 } };
        const float height = ImGui::GetFrameHeight();
        const float axisWidth = ImGui::GetContentRegionAvail().x / 3.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
        for (int i = 0; i < 3; ++i)
        {
            if (i) ImGui::SameLine();
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, colors[i]);
            if (ImGui::Button(axes[i], { height, height })) values[i] = resetValue;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(1.0f, axisWidth - height - 1));
            ImGui::DragFloat("##axis", &values[i], .1f, 0, 0, "%.2f");
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::Columns(1);

        ImGui::PopID();
    }

    static void BeginScriptFieldRow(const std::string& label, float columnWidth = 100.0f)
    {
        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::TextUnformatted(label.c_str());
        ImGui::NextColumn();
    }

    static void EndScriptFieldRow()
    {
        ImGui::Columns(1);
    }

    static bool DrawScriptFloatControl(const std::string& label, float& value)
    {
        BeginScriptFieldRow(label);
        ImGui::PushItemWidth(-1.0f);
        PushScriptFieldFillStyle();
        bool changed = ImGui::DragFloat("##value", &value, 0.1f);
        PopScriptFieldFillStyle();
        ImGui::PopItemWidth();
        EndScriptFieldRow();
        return changed;
    }

    static bool DrawScriptIntControl(const std::string& label, int& value)
    {
        BeginScriptFieldRow(label);
        ImGui::PushItemWidth(-1.0f);
        PushScriptFieldFillStyle();
        bool changed = ImGui::DragInt("##value", &value);
        PopScriptFieldFillStyle();
        ImGui::PopItemWidth();
        EndScriptFieldRow();
        return changed;
    }

    static bool DrawScriptBoolControl(const std::string& label, bool& value)
    {
        BeginScriptFieldRow(label);
        PushScriptFieldFillStyle();
        bool changed = ImGui::Checkbox("##value", &value);
        PopScriptFieldFillStyle();
        EndScriptFieldRow();
        return changed;
    }

    // 编辑器模式下的字段控件：直接读写 ScriptEngine 维护的字节缓存
    // 与 DrawRuntimeScriptFieldControl 不同，这里不存在 ScriptInstance，值进入 Play 时才生效
    static void DrawEditorScriptFieldControl(const ScriptField& field, ScriptComponent::FieldMap& fieldCache, Scene* scene)
    {
        const size_t typeSize = ScriptFieldTypeSize(field.Type);
        if (typeSize == 0)
        {
            ImGui::TextDisabled("%s (%s)", field.Name.c_str(), ScriptFieldTypeToString(field.Type));
            return;
        }

        // 优先显示用户给这个实体填的值。还没填过时，显示 C# 脚本里的初始值。
        // 只有在面板里修改字段才保存，展开面板不会改变脚本的初始值。
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
                changed = DrawScriptFloatControl(field.Name, *reinterpret_cast<float*>(buffer.data()));
                break;
            case ScriptFieldType::Int:
                changed = DrawScriptIntControl(field.Name, *reinterpret_cast<int*>(buffer.data()));
                break;
            case ScriptFieldType::Bool:
                changed = DrawScriptBoolControl(field.Name, *reinterpret_cast<bool*>(buffer.data()));
                break;
            case ScriptFieldType::Vector3:
            {
                glm::vec3* vecPtr = reinterpret_cast<glm::vec3*>(buffer.data());
                glm::vec3 previousValue = *vecPtr;
                DrawVec3Control(field.Name, *vecPtr);
                changed = (previousValue != *vecPtr);
                break;
            }
            case ScriptFieldType::Entity:
            {
                uint64_t entityID = ReadEntityIDFromBuffer(buffer);
                changed = DrawEntityReferenceControl(field.Name, scene, entityID);
                if (changed)
                    WriteEntityIDToBuffer(buffer, entityID);
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

    static void DrawRuntimeScriptFieldControl(const ScriptField& field, const Ref<ScriptInstance>& scriptInstance, Scene* scene)
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

                if (DrawScriptFloatControl(field.Name, value))
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

                if (DrawScriptIntControl(field.Name, value))
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

                if (DrawScriptBoolControl(field.Name, value))
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
            case ScriptFieldType::Entity:
            {
                uint64_t entityID = 0;
                if (!scriptInstance->GetFieldValue(field, &entityID))
                {
                    ImGui::TextDisabled("%s", field.Name.c_str());
                    break;
                }

                uint64_t previousEntityID = entityID;
                if (DrawEntityReferenceControl(field.Name, scene, entityID) && entityID != previousEntityID)
                    scriptInstance->SetFieldValue(field, &entityID);
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
        if (!entity.HasComponent<T>()) return;

        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::PushID((void*)typeid(T).hash_code());
        ImGui::Separator();
        bool open = ImGui::TreeNodeEx("##component", flags, "%s", name.c_str());
        bool remove = false;

        // 每个场景对象都需要变换，其他组件可以从标题栏删除。
        if constexpr (!std::is_same_v<T, TransformComponent>)
        {
            ImGui::SameLine(right - ImGui::CalcTextSize("…").x - 2 * ImGui::GetStyle().FramePadding.x);
            if (ImGui::SmallButton("…")) ImGui::OpenPopup("ComponentSettings");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("组件选项");
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                remove = ImGui::MenuItem("移除组件");
                ImGui::EndPopup();
            }
        }
        if (open)
        {
            if constexpr (entt::component_traits<T>::page_size == 0)
                uiFunction();
            else
                uiFunction(entity.GetComponent<T>());
            ImGui::TreePop();
        }
        if (remove) entity.RemoveComponent<T>();
        ImGui::PopID();
    }

    template<typename T>
    static bool AddComponentItem(Entity entity, const char* name)
    {
        if (entity.HasComponent<T>() || !ImGui::MenuItem(name)) return false;
        entity.AddComponent<T>();
        return true;
    }

    static void DrawEmission(Material& material)
    {
        if (ImGui::ColorEdit3("自发光", glm::value_ptr(material.Emission), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            material.Emission = glm::max(material.Emission, glm::vec3(0));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("这三个数是表面自己发出的红、绿、蓝光。可输入大于 1 的值，三项都为 0 时熄灭。");
        if (ImGui::Button("选择自发光贴图"))
        {
            const std::string file = FileDialogs::OpenFile("Image (*.png;*.jpg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
            if (!file.empty())
            {
                const Ref<Texture2D> texture = Texture2D::Create(file);
                if (texture->IsLoaded()) material.EmissiveMap = texture;
            }
        }
        if (material.EmissiveMap)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("移除##Emission")) material.EmissiveMap.reset();
        }
        if (material.EmissiveMap)
        {
            const std::filesystem::path file = material.EmissiveMap->GetPath();
            ImGui::TextDisabled("%s", file.empty() ? "使用模型内的图片" : file.filename().u8string().c_str());
        }
    }

    static void DrawTransparency(Material& material)
    {
        const char* modes[] = { "不透明", "透明裁剪", "半透明" };
        int mode = static_cast<int>(material.Alpha);
        if (ImGui::Combo("表面类型", &mode, modes, IM_ARRAYSIZE(modes)))
            material.Alpha = static_cast<AlphaMode>(mode);
        ImGui::Checkbox("双面绘制", &material.DoubleSided);

        if (material.Alpha == AlphaMode::Mask)
            ImGui::SliderFloat("裁剪阈值", &material.AlphaCutoff, 0.0f, 1.0f);
        if (material.Alpha == AlphaMode::Blend)
        {
            if (ImGui::InputFloat("折射率", &material.RefractionIndex))
                material.RefractionIndex = std::max(1.0f, material.RefractionIndex);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("空气取 1，普通玻璃约为 1.5。");
            if (ImGui::InputFloat("厚度", &material.Thickness))
                material.Thickness = std::max(0.0f, material.Thickness);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("使用场景长度单位。厚度为零时，背景保持在原位置。");
        }
    }

    static void EditText(const char* label, std::string& text)
    {
        ImGui::InputText(label, text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* change) {
                auto& value = *static_cast<std::string*>(change->UserData);
                value.resize(change->BufTextLen);
                change->Buf = value.data();
                return 0;
            }, &text);
    }

    void SceneHierachyPanel::DrawComponents(Entity entity)
    {
        if (!entity) return;

       
        const float addWidth = ImGui::CalcTextSize("添加组件").x + ImGui::GetStyle().FramePadding.x * 2;
        // Tag组件
        if (entity.HasComponent<TagComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy_s(buffer, sizeof(buffer), tag.c_str());

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            ImGui::PushItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - addWidth - spacing));
            if (ImGui::InputText("##ReName", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
            ImGui::PopItemWidth();
        }

        ImGui::SameLine();
        if (ImGui::Button("添加组件", ImVec2(addWidth, 0)))
            ImGui::OpenPopup("Add Comp");

        if (ImGui::BeginPopup("Add Comp"))
        {
            AddComponentItem<CameraComponent>(entity, "相机  Camera");
            AddComponentItem<PointLightComponent>(entity, "点光源  Point Light");
            if (AddComponentItem<MeshRendererComponent>(entity, "网格绘制"))
                entity.GetComponent<MeshRendererComponent>().MeshData = Mesh::CreateCube();
            AddComponentItem<ModelRendererComponent>(entity, "模型绘制");
            AddComponentItem<FoliageComponent>(entity, "植被");
            AddComponentItem<TerrainComponent>(entity, "地形");
            AddComponentItem<DecalComponent>(entity, "贴花");
            AddComponentItem<EnvironmentZoneComponent>(entity, "环境区域");
            AddComponentItem<ModelAnimationComponent>(entity, "模型动画");
            AddComponentItem<SkeletonAnimationComponent>(entity, "骨骼动画");
            if (!entity.HasComponent<RigidBody2DComponent>() && !entity.HasComponent<RigidBody3DComponent>())
                AddComponentItem<RagdollComponent>(entity, "物理角色（双腿）");
            if (!entity.HasComponent<RigidBody2DComponent>() && !entity.HasComponent<RagdollComponent>() && AddComponentItem<RigidBody3DComponent>(entity, "3D 刚体"))
            {
                if (!entity.HasComponent<BoxCollider3DComponent>() && !entity.HasComponent<CapsuleCollider3DComponent>())
                    entity.AddComponent<BoxCollider3DComponent>();
            }
            if (!entity.HasComponent<CapsuleCollider3DComponent>())
                AddComponentItem<BoxCollider3DComponent>(entity, "3D 盒形碰撞体");
            if (!entity.HasComponent<BoxCollider3DComponent>())
                AddComponentItem<CapsuleCollider3DComponent>(entity, "3D 胶囊碰撞体");
            AddComponentItem<PickupComponent>(entity, "可拿取");
            AddComponentItem<HealthComponent>(entity, "生命与耐久");
            AddComponentItem<NpcComponent>(entity, "AI 角色");
            AddComponentItem<InventoryComponent>(entity, "库存");
            AddComponentItem<HudComponent>(entity, "游戏界面");
            AddComponentItem<InteractionComponent>(entity, "持续交互");

            const char* extra = RendererAPI::GetAPI() == RendererAPI::API::Vulkan ? "其他组件（OpenGL 预览）" : "更多组件";
            if (ImGui::BeginMenu(extra))
            {
                AddComponentItem<DirectionalLightComponent>(entity, "方向光");
                AddComponentItem<ScriptComponent>(entity, "脚本");
                AddComponentItem<SpriteRendererComponent>(entity, "2D 精灵");
                AddComponentItem<CircleRendererComponent>(entity, "2D 圆形");
                if (!entity.HasComponent<RigidBody3DComponent>() && !entity.HasComponent<RagdollComponent>())
                    AddComponentItem<RigidBody2DComponent>(entity, "2D 刚体");
                AddComponentItem<BoxCollider2DComponent>(entity, "2D 方形碰撞体");
                AddComponentItem<CircleCollider2DComponent>(entity, "2D 圆形碰撞体");
                AddComponentItem<ParticleEmitterComponent>(entity, "2D 粒子发射器");
                AddComponentItem<TextureAnimationComponent>(entity, "贴图动画");
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();

        // Transform组件
        DrawComponent<TransformComponent>("变换  Transform", entity, [](auto& component)
            {
                DrawVec3Control("位置", component.Translation);
                const glm::vec3 degrees = glm::degrees(component.Rotation);
                glm::vec3 edited = degrees;
                DrawVec3Control("旋转", edited);
                // 面板显示角度，场景保存弧度。只改用户动过的轴，单纯选中物体也能沿用原来的烘焙。
                for (int axis = 0; axis < 3; ++axis)
                    if (edited[axis] != degrees[axis])
                        component.Rotation[axis] = glm::radians(edited[axis]);
                DrawVec3Control("缩放", component.Scale, 1.0f);
            });

        // Camera组件
        DrawComponent<CameraComponent>("相机  Camera", entity, [](auto& component)
            {
                auto& camera = component.Camera;

                ImGui::Checkbox("主相机", &component.Primary);

                const char* projectionTypeStrings[] = { "透视", "正交" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];

                if (ImGui::BeginCombo("投影方式", currentProjectionTypeString))
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
                    if (ImGui::DragFloat("垂直视角", &verticalFov))
                        camera.SetPerspectiveFOV(glm::radians(verticalFov));

                    float perspectiveNear = camera.GetPerspectiveNearClip();
                    if (ImGui::DragFloat("近裁剪面", &perspectiveNear))
                        camera.SetPerspectiveNearClip(perspectiveNear);

                    float perspectiveFar = camera.GetPerspectiveFarClip();
                    if (ImGui::DragFloat("远裁剪面", &perspectiveFar))
                        camera.SetPerspectiveFarClip(perspectiveFar);
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                {
                    float orthoSize = camera.GetOrthographicSize();
                    if (ImGui::DragFloat("视野高度", &orthoSize))
                        camera.SetOrthographicSize(orthoSize);

                    float orthoNear = camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("近裁剪面", &orthoNear))
                        camera.SetOrthographicNearClip(orthoNear);

                    float orthoFar = camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("远裁剪面", &orthoFar))
                        camera.SetOrthographicFarClip(orthoFar);

                    ImGui::Checkbox("固定宽高比", &component.FixedAspectRatio);
                }

            });

        // Script 组件面板
        DrawComponent<ScriptComponent>("脚本", entity, [entity, this](auto& component)
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
					auto& fieldCache = component.Fields;
					for (const ScriptField& field : fields)
						DrawEditorScriptFieldControl(field, fieldCache, m_Context.get());
				}
                else
                {
                    for (const ScriptField& field : fields)
                        DrawRuntimeScriptFieldControl(field, scriptInstance, m_Context.get());
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

                static bool showSourcePreview = false;
                if (ImGui::SmallButton(showSourcePreview ? "Hide Source" : "Show Source"))
                    showSourcePreview = !showSourcePreview;

                if (showSourcePreview)
                {
                    ImGui::BeginChild("ScriptSourceView", ImVec2(0.0f, 160.0f), false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    ImGui::TextUnformatted(sourceEntry.Contents.c_str());
                    ImGui::EndChild();
                }
            });


        // SpriteRender
        DrawComponent<SpriteRendererComponent>("2D 精灵", entity, [](auto& component)
            {
                // 颜色编辑
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

                // 平铺因子
                ImGui::DragFloat("Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.f);

                // 可把资源浏览器里的贴图拖到预览上。
                ImGui::Separator();
                ImGui::Text("Texture");

                // 显示当前纹理预览
                if (component.Texture && component.Texture->GetRendererID())
                {
                    // 纹理预览
                    ImGui::Image(
                        (void*)(intptr_t)component.Texture->GetRendererID(),
                        ImVec2(64, 64)
                    );
                }
                else
                {
                    // 空纹理占位
                    ImGui::Button("No Texture", ImVec2(64, 64));
                }

                // 拖拽目标
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

                // 清除纹理按钮
                if (component.Texture)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Texture"))
                    {
                        component.Texture = nullptr;
                    }
                }
                // ----------
            });


        //CircleRendererComponent
        DrawComponent<CircleRendererComponent>("2D 圆形", entity, [](auto& component)
            {
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
                ImGui::DragFloat("Thickness", &component.Thickness, 0.025f, 0.f, 1.f);
                ImGui::DragFloat("Fade", &component.Fade, 0.0025f, 2.f, 1.f);
            });

        DrawComponent<SkeletonAnimationComponent>("骨骼动画", entity, [](SkeletonAnimationComponent& player)
            {
                if (!player.File.empty()) ImGui::TextWrapped("%s", player.File.filename().u8string().c_str());
                if (ImGui::Button("选择 BVH 动作…"))
                {
                    const auto path = FileDialogs::OpenFile("BVH motion (*.bvh)\0*.bvh\0");
                    if (!path.empty())
                    {
                        try { player.Load(std::filesystem::path(path)); }
                        catch (const std::exception& error) { RF_CORE_WARN("BVH: {0}", error.what()); ImGui::OpenPopup("动作文件无法读取"); }
                    }
                }
                if (ImGui::BeginPopup("动作文件无法读取"))
                {
                    ImGui::TextUnformatted("无法读取所选的 BVH。请检查动作文件。");
                    ImGui::EndPopup();
                }
                if (!player.Clip)
                {
                    if (!player.File.empty()) ImGui::TextDisabled("找不到或无法读取此动作，请重新选择。");
                    return;
                }
                ImGui::Checkbox("循环播放", &player.Playing);
                float time = player.Time;
                if (ImGui::SliderFloat("进度（秒）", &time, 0.f, player.Clip->Duration() - player.Clip->FrameTime, "%.2f"))
                {
                    player.Playing = false;
                    player.Seek(time);
                }
                ImGui::Separator();
                ImGui::TextUnformatted("角色控制");
                if (!player.IdleFile.empty())
                    ImGui::TextWrapped("%s", player.IdleFile.filename().u8string().c_str());
                if (ImGui::Button("选择待机 BVH…"))
                {
                    const auto file = FileDialogs::OpenFile("BVH motion (*.bvh)\0*.bvh\0");
                    if (!file.empty())
                    {
                        try { player.SetIdleMotion(file); }
                        catch (const std::exception& error) { RF_CORE_WARN("BVH: {0}", error.what()); ImGui::OpenPopup("待机动作不兼容"); }
                    }
                }
                if (ImGui::BeginPopup("待机动作不兼容"))
                {
                    ImGui::TextWrapped("请选择同一骨架的待机与行走动作。行走动作须包含向前的位移。");
                    ImGui::EndPopup();
                }
                if (!player.IdleFile.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::Button("移除待机动作")) player.SetIdleMotion({});
                }
                if (player.HasControl())
                {
                    float speed = player.MoveSpeed;
                    if (ImGui::DragFloat("行走速度（米/秒）", &speed, .05f) && speed >= 0.f) player.MoveSpeed = speed;
                    ImGui::TextWrapped("启动模拟或游戏，在视口中按方向键行走，松开后站立。");
                }
            });

        DrawComponent<PickupComponent>("可拿取", entity, [](PickupComponent& pickup)
            {
                EditText("物品编号", pickup.Item);
                ImGui::InputInt("数量", &pickup.Count);
            });

        DrawComponent<NpcComponent>("AI 角色", entity, [this, entity](NpcComponent& npc)
            {
                EditText("角色定义", npc.Definition);
                ImGui::InputScalar("队友实体编号", ImGuiDataType_U64, &npc.Leader);
                EditText("覆盖音色", npc.VoiceOverride);
                if (npc.VoiceOverride.empty()) ImGui::TextDisabled("使用角色定义中的音色");
                if (ImGui::TreeNode("当前位置的环境"))
                {
                    const std::vector<DecisionFact> facts = SenseEnvironment(*m_Context, entity.GetUUID());
                    if (facts.empty()) ImGui::TextDisabled("角色在所有环境区域之外");
                    for (const DecisionFact& fact : facts) ImGui::TextWrapped("%s", fact.Description.c_str());
                    ImGui::TreePop();
                }
            });

        DrawComponent<EnvironmentZoneComponent>("环境区域", entity, [this](EnvironmentZoneComponent& zone)
        {
            ImGui::DragFloat3("区域半长（米）", &zone.HalfSize.x, .1f, .01f, 100000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("分别是从中心到 X、Y、Z 三个面的距离。实体的变换决定区域的位置和朝向。");
            EditText("地貌", zone.Landform);
            EditText("植被", zone.Vegetation);
            EditText("地标", zone.Landmark);
            if (ImGui::BeginCombo("关联灯具", "选择场景里的灯"))
            {
                for (entt::entity handle : m_Context->GetRegistry().view<TagComponent>())
                {
                    Entity lamp(handle, m_Context.get());
                    if (!lamp.HasComponent<PointLightComponent>() && !lamp.HasComponent<DirectionalLightComponent>()) continue;
                    const uint64_t id = lamp.GetUUID();
                    const bool linked = zone.Lights.count(id) != 0;
                    ImGui::PushID(static_cast<int>(handle));
                    if (ImGui::Selectable(lamp.GetName().c_str(), linked))
                    {
                        if (linked) zone.Lights.erase(id);
                        else zone.Lights.insert(id);
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            for (auto it = zone.Lights.begin(); it != zone.Lights.end(); )
            {
                Entity lamp = m_Context->GetEntityByUUID(*it);
                const std::string id = std::to_string(*it);
                ImGui::PushID(id.c_str());
                ImGui::TextUnformatted(lamp ? lamp.GetName().c_str() : id.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("移除")) it = zone.Lights.erase(it);
                else ++it;
                ImGui::PopID();
            }
        });

        DrawComponent<HealthComponent>("生命与耐久", entity, [](HealthComponent& health)
            {
                ImGui::DragFloat("最大值", &health.Maximum);
                ImGui::DragFloat("当前值", &health.Current);
                EditText("阵营", health.Team);
            });

        DrawComponent<InteractionComponent>("持续交互", entity, [](InteractionComponent& use)
            {
                EditText("动作", use.Action);
                ImGui::DragFloat("距离（米）", &use.Range, .1f);
                ImGui::DragFloat("持续时间（秒）", &use.Seconds, .1f);
                ImGui::DragFloat("完成时恢复", &use.Restore);
                ImGui::Checkbox("仅在目标生命为零时可用", &use.OnlyWhenDown);
            });

        DrawComponent<HudComponent>("游戏界面", entity, [](HudComponent& hud) {
            EditText("布局文件", hud.Layout);
        });

        DrawComponent<InventoryComponent>("库存", entity, [this](InventoryComponent& inventory)
            {
                for (auto it = inventory.Items.begin(); it != inventory.Items.end(); )
                {
                    ImGui::PushID(it->first.c_str());
                    ImGui::InputInt(it->first.c_str(), &it->second);
                    if (ImGui::SmallButton("移除")) it = inventory.Items.erase(it);
                    else ++it;
                    ImGui::PopID();
                }
                EditText("物品编号", m_NewItem);
                if (ImGui::Button("添加物品") && !m_NewItem.empty())
                {
                    inventory.Items.emplace(m_NewItem, 1);
                    m_NewItem.clear();
                }
            });

        DrawComponent<RagdollComponent>("物理角色（双腿）", entity, [](RagdollComponent& character)
            {
                ImGui::Checkbox("行走驱动", &character.Walking);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("关闭后放松关节，两腿和骨盆由重力带动。");
                ImGui::DragFloat("速度（米/秒）", &character.Speed, .05f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("沿开始时的朝向前进，零站住，负值后退。");
                ImGui::TextWrapped("变换位置在骨盆中心。平地站立时，骨盆离地约 0.91 米。七个碰撞盒使用固定尺寸。");
            });

        DrawComponent<RigidBody3DComponent>("3D 刚体", entity, [](RigidBody3DComponent& body)
            {
                const char* types[] = { "静态", "动态", "运动学" };
                int selected = static_cast<int>(body.Type);
                if (ImGui::Combo("类型", &selected, types, IM_ARRAYSIZE(types)))
                    body.Type = static_cast<BodyType3D>(selected);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("静态用于地面，动态物体会掉落，运动学物体跟随场景设置的位置和旋转。");
                if (body.Type == BodyType3D::Dynamic)
                {
                    float density = body.Density;
                    if (ImGui::DragFloat("密度（kg/m³）", &density, 0.1f) && density > 0.f)
                        body.Density = density;
                }
            });

        DrawComponent<BoxCollider3DComponent>("3D 盒形碰撞体", entity, [&](BoxCollider3DComponent& box)
            {
                glm::vec3 size = box.HalfSize * 2.f;
                ImGui::DragFloat3("中心偏移（米）", glm::value_ptr(box.Center), .02f);
                if (ImGui::DragFloat3("尺寸 XYZ（米）", glm::value_ptr(size), 0.01f) &&
                    glm::all(glm::greaterThan(size, glm::vec3(0))))
                    box.HalfSize = size * .5f;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("三个数分别沿本地 X、Y、Z 轴。模拟时还会乘以物体的缩放。");
                if (ImGui::Button("改为胶囊"))
                {
                    auto& capsule = entity.AddComponent<CapsuleCollider3DComponent>();
                    capsule.Center = box.Center;
                    capsule.Radius = std::min(box.HalfSize.x, box.HalfSize.z);
                    capsule.Height = 2.f * std::max(box.HalfSize.y, capsule.Radius);
                    entity.RemoveComponent<BoxCollider3DComponent>();
                }
            });

        DrawComponent<CapsuleCollider3DComponent>("3D 胶囊碰撞体", entity, [&](CapsuleCollider3DComponent& capsule)
            {
                ImGui::DragFloat3("中心偏移（米）", glm::value_ptr(capsule.Center), .01f);
                int axis = int(capsule.Axis);
                const char* axes[] = { "X", "Y", "Z" };
                if (ImGui::Combo("长轴", &axis, axes, IM_ARRAYSIZE(axes)))
                    capsule.Axis = static_cast<CapsuleAxis>(axis);

                float radius = capsule.Radius, height = capsule.Height;
                if (ImGui::DragFloat("半径（米）", &radius, .01f) && radius > 0.f)
                {
                    capsule.Radius = radius;
                    capsule.Height = std::max(capsule.Height, 2.f * radius);
                }
                if (ImGui::DragFloat("总长（米）", &height, .01f))
                    capsule.Height = std::max(height, 2.f * capsule.Radius);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("总长从一个圆头量到另一个圆头，最短等于直径。");
                if (ImGui::Button("改为盒形"))
                {
                    auto& box = entity.AddComponent<BoxCollider3DComponent>();
                    box.Center = capsule.Center;
                    box.HalfSize = glm::vec3(capsule.Radius);
                    box.HalfSize[axis] = .5f * capsule.Height;
                    entity.RemoveComponent<CapsuleCollider3DComponent>();
                }
            });

        // Rigidbody2D 组件
        DrawComponent<RigidBody2DComponent>("2D 刚体", entity, [](auto& component)
            {
                // 刚体类型下拉框
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

                // 固定旋转
                ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
            });

        // BoxCollider2D 组件
        DrawComponent<BoxCollider2DComponent>("2D 方形碰撞体", entity, [](auto& component)
            {
                // 偏移量
                ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
                // 碰撞盒尺寸
                ImGui::DragFloat2("Size", glm::value_ptr(component.Size));
                // 密度
                ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
                // 摩擦力
                ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
                // 弹性
                ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
                // 速度单位是米/秒。接触面两侧靠近得足够快才会反弹。
                ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
            });

        // CircleCollider2D 属性面板
        DrawComponent<CircleCollider2DComponent>("2D 圆形碰撞体", entity, [](auto& component)
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
                // 慢慢贴在一起时跳过反弹，提高这个速度会让物体更容易停稳。
                ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
            });

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>("网格绘制", entity, [](auto& component)
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
                DrawEmission(*mat);
                DrawTransparency(*mat);

                // ---- 查看和更换材质贴图 ----
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
                DrawHeightMap(*mat);
                TextureSlotUI("MetallicRoughness Map",  mat->MetallicRoughnessMap);
                TextureSlotUI("AO Map",                 mat->AOMap);
            });

        // Model Renderer
        DrawComponent<ModelAnimationComponent>("模型动画", entity, [entity](ModelAnimationComponent& player) mutable
        {
            if (!entity.HasComponent<ModelRendererComponent>()) {
                ImGui::TextWrapped("请先添加“模型绘制”，并选择带动作的 glTF 模型。");
                return;
            }
            ModelRendererComponent& renderer = entity.GetComponent<ModelRendererComponent>();
            player.Update(renderer.ModelAsset, 0.f);
            if (!player.GetAnimation()) {
                ImGui::TextWrapped("此模型没有可用的 glTF 节点或骨骼动作。");
                return;
            }
            const auto& clips = player.GetAnimation()->GetClips();
            if (ImGui::BeginCombo("动作", player.Clip.c_str())) {
                for (const auto& clip : clips)
                    if (ImGui::Selectable(clip.Name.c_str(), player.Clip == clip.Name)) {
                        player.Clip = clip.Name;
                        player.Time = 0.f;
                    }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("播放", &player.Playing);
            for (const auto& clip : clips)
                if (clip.Name == player.Clip && ImGui::SliderFloat("时间（秒）", &player.Time, 0.f, clip.Duration, "%.3f"))
                    player.Playing = false;
        });

        DrawComponent<DecalComponent>("贴花", entity, [](DecalComponent& decal)
        {
            if (ImGui::Button("选择贴花图片"))
            {
                const std::string file = FileDialogs::OpenFile("Decal image\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0");
                if (!file.empty()) decal.Texture = std::filesystem::proximate(std::filesystem::path(file)).generic_u8string();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* image = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                    decal.Texture = std::filesystem::path(static_cast<const char*>(image->Data)).generic_u8string();
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(decal.Texture.empty() ? "未选择图片" : std::filesystem::u8path(decal.Texture).filename().u8string().c_str());
            ImGui::DragFloat3("宽、厚、高（米）", glm::value_ptr(decal.Size), .01f, .001f, 10000.f);
            ImGui::ColorEdit4("着色与透明度", glm::value_ptr(decal.Tint));
            ImGui::SliderFloat("粗糙度", &decal.Roughness, 0.f, 1.f);
            ImGui::SliderFloat("金属度", &decal.Metallic, 0.f, 1.f);
            ImGui::SliderFloat("最大倾角", &decal.MaxAngle, 1.f, 90.f, "%.0f°");
            ImGui::InputInt("叠放顺序", &decal.Order);
            ImGui::TextDisabled("默认向下投影。在变换组件中旋转，可将图案投到墙上。");
        });

        DrawComponent<FoliageComponent>("植被", entity, [](FoliageComponent& patch)
        {
            ImGui::TextWrapped("使用同一实体上“模型绘制”选定的模型。");
            ImGui::Checkbox("显示植被", &patch.Visible);
            ImGui::SameLine();
            ImGui::Checkbox("风吹植被", &patch.Wind);
            if (ImGui::Button("选择植被配置"))
            {
                const std::string file = FileDialogs::OpenFile("Foliage YAML\0*.foliage.yaml;*.yaml\0");
                if (!file.empty()) patch.Profile = std::filesystem::relative(std::filesystem::u8path(file)).generic_u8string();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(std::filesystem::u8path(patch.Profile).filename().u8string().c_str());
            Foliage* grass = patch.Get();
            if (grass)
            {
                bool changed = false;
                int count = int(grass->Count);
                if (ImGui::DragInt("株数", &count, 1.f, 0, 65536, "%d", ImGuiSliderFlags_AlwaysClamp))
                {
                    grass->Count = uint32_t(count);
                    changed = true;
                }
                changed |= ImGui::InputScalar("分布种子", ImGuiDataType_U32, &grass->Seed);
                changed |= ImGui::DragFloat2("覆盖范围（米）", &grass->Size.x, .1f, 0.f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::DragFloat("植株缩放", &grass->Scale, .01f, .001f, 100.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::DragFloat("模型中根部的高度", &grass->Root, .01f);
                changed |= ImGui::DragFloat2("风向（X、Z）", &grass->Direction.x, .02f);
                changed |= ImGui::DragFloat("每米植株的摆幅（米）", &grass->Lean, .01f, 0.f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::DragFloat("每秒摆动次数", &grass->Frequency, .01f, 0.f, 20.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                changed |= ImGui::DragFloat("两阵风的间距（米）", &grass->Wavelength, .1f, .01f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                if (changed) ++patch.Revision;
                if (ImGui::Button("保存植被配置")) grass->Save(std::filesystem::u8path(patch.Profile), patch.Error);
                ImGui::SameLine();
            }
            if (!patch.Profile.empty() && ImGui::Button("重新读取配置")) patch.Reload();
            if (!patch.Error.empty()) ImGui::TextWrapped("%s", patch.Error.c_str());
        });

        DrawComponent<TerrainComponent>("地形", entity, [](TerrainComponent& ground)
        {
            auto choose = [](const char* label, std::string& path, const char* filter)
            {
                ImGui::PushID(label);
                bool changed = false;
                if (ImGui::Button(label))
                {
                    const std::string file = FileDialogs::OpenFile(filter);
                    if (!file.empty())
                    {
                        path = std::filesystem::relative(std::filesystem::u8path(file)).generic_u8string();
                        changed = true;
                    }
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* drop = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                    {
                        path = static_cast<const char*>(drop->Data);
                        changed = true;
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(path.empty() ? "未选择" : std::filesystem::u8path(path).filename().u8string().c_str());
                ImGui::PopID();
                return changed;
            };

            if (choose("选择高度图", ground.Heightmap, "Grayscale PNG\0*.png\0")) ground.Reload();
            choose("选择地表贴图", ground.BaseColorTexture, "Surface image\0*.jpg;*.jpeg;*.png;*.tga\0");
            if (!ground.Heightmap.empty() && ImGui::Button("重新读取高度图")) ground.Reload();
            const Ref<Terrain> data = ground.GetTerrain();
            if (data)
                ImGui::Text("采样点 %u × %u，地面 %.1f × %.1f 米", data->GetWidth(), data->GetDepth(),
                    (data->GetWidth() - 1) * ground.CellSize, (data->GetDepth() - 1) * ground.CellSize);
            else if (!ground.Heightmap.empty()) ImGui::TextUnformatted("无法读取高度图，请选择 8 位或 16 位单通道 PNG。");

            ImGui::DragFloat("相邻采样点间距（米）", &ground.CellSize, .1f, .01f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("地形高差（米）", &ground.HeightScale, .1f, 0.f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("贴图宽度（米）", &ground.UVScaleMeters, .1f, .01f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            int cells = int(ground.ChunkCells);
            if (ImGui::DragInt("每块一边的格数", &cells, 1.f, 1, 256, "%d", ImGuiSliderFlags_AlwaysClamp))
                ground.ChunkCells = uint32_t(cells);
            ImGui::ColorEdit3("地表颜色", &ground.Tint.x);
            ImGui::Checkbox("地形碰撞", &ground.Collision);
        });

        DrawComponent<ModelRendererComponent>("模型绘制", entity, [](auto& component)
            {
                // 文件路径（只读显示）
                std::filesystem::path file(component.FilePath);
                ImGui::Text("模型文件  %s", component.FilePath.empty() ? "从项目中拖入模型" : file.filename().u8string().c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", component.FilePath.c_str());

                // 拖拽接受区：从 Content Browser 拖入模型文件
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL"))
                    {
                        const char* path = (const char*)payload->Data;
                        if (path)
                        {
                            try
                            {
                                auto id = AssetManager::Add<Model>(path);
                                auto model = AssetManager::Load<Model>(id);
                                if (model)
                                {
                                    component.FilePath = path;
                                    component.ModelID = id;
                                    component.ModelAsset = model;
                                    component.OverrideMaterials.clear();
                                    component.LODs.clear();
                                    component.ActiveLOD = 0;
                                }
                            }
                            catch (const std::exception& error)
                            {
                                RF_CORE_ERROR("Cannot load model: {0}", error.what());
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 备用颜色
                ImGui::TextUnformatted("整体颜色");
                ImGui::SetNextItemWidth(-1);
                ImGui::ColorEdit4("##ModelTint", glm::value_ptr(component.Color));

                // 卸载按钮
                if (component.ModelAsset)
                {
                    if (ImGui::Button("移除模型文件"))
                    {
                        AssetManager::Unload<Model>(component.ModelID);
                        component.ModelAsset.reset();
                        component.ModelID = 0;
                        component.FilePath.clear();
                        component.OverrideMaterials.clear();
                        component.LODs.clear();
                        component.ActiveLOD = 0;
                    }
                }

                const auto lodFlags = component.LODs.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen;
                if (component.ModelAsset && ImGui::TreeNodeEx("网格细节 LOD", lodFlags))
                {
                    DrawModelLODs(component);
                    ImGui::TreePop();
                }

                // ---- 模型自带材质信息（只读预览）----
                if (component.ModelAsset)
                {
                    const auto& subMeshes = component.ModelAsset->GetSubMeshes();
                    if (!subMeshes.empty() && ImGui::TreeNode("模型自带材质"))
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
                                    ImGui::Text("自发光 RGB  %.2f  %.2f  %.2f", sub.Mat->Emission.r,
                                        sub.Mat->Emission.g, sub.Mat->Emission.b);
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
                if (ImGui::Checkbox("为此物体调整材质", &hasOverride))
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
                    ImGui::BeginChild("OverrideMaterialsList", ImVec2(0.0f, 320.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

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
                        if (ImGui::TreeNode("SubMeshMat", "部件 %d 的材质", (int)i + 1))
                        {
                            ImGui::ColorEdit4("Base Color", glm::value_ptr(mat->BaseColor));
                            ImGui::SliderFloat("Metallic",  &mat->Metallic,  0.0f, 1.0f);
                            ImGui::SliderFloat("Roughness", &mat->Roughness, 0.0f, 1.0f);
                            ImGui::SliderFloat("AO",        &mat->AO,        0.0f, 1.0f);
                            DrawEmission(*mat);
                            DrawTransparency(*mat);

                            TextureSlotUI("BaseColor Map",          mat->BaseColorMap);
                            TextureSlotUI("Normal Map",             mat->NormalMap);
                            DrawHeightMap(*mat);
                            TextureSlotUI("MetallicRoughness Map",  mat->MetallicRoughnessMap);
                            TextureSlotUI("AO Map",                 mat->AOMap);

                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    ImGui::EndChild();
                }
            });

        // Directional Light 属性面板
        DrawComponent<DirectionalLightComponent>("方向光", entity, [](auto& component)
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
        DrawComponent<PointLightComponent>("点光源  Point Light", entity, [](auto& component)
            {
                // 光源颜色
                ImGui::ColorEdit3("颜色", glm::value_ptr(component.Color));
                // 辐照度缩放
                ImGui::DragFloat("强度", &component.Intensity, 0.05f, 0.0f, 100.0f);
                if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan)
                    return;
                // 影响半径
                ImGui::DragFloat("Radius", &component.Radius, 0.1f, 0.01f, 1000.0f);
                // 是否投射阴影（点光用立方体 Shadow Map，开销较大）
                ImGui::Checkbox("Cast Shadow", &component.CastShadow);
            });

        // Particle Emitter 2D 属性面板
        DrawComponent<ParticleEmitterComponent>("2D 粒子发射器", entity, [](auto& component)
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
        DrawComponent<TextureAnimationComponent>("贴图动画", entity, [](auto& component)
            {
                ImGui::DragInt("Columns",    &component.Columns,        1.0f, 1, 64);
                ImGui::DragInt("Rows",       &component.Rows,           1.0f, 1, 64);
                ImGui::DragInt("Frame Count", &component.FrameCount,    1.0f, 1, 4096);
                ImGui::DragFloat("Speed (FPS)", &component.AnimationSpeed, 0.5f, 0.0f, 120.0f);
                ImGui::Checkbox("Loop", &component.Loop);

                ImGui::Separator();
                ImGui::Text("Runtime: Frame %d / %d", component.CurrentFrame, component.FrameCount);
            });

        if (ImGui::TreeNode("调试信息"))
        {
            DrawReadOnlyTextControl("UUID", std::to_string((uint64_t)entity.GetUUID()));
            DrawReadOnlyTextControl("Entity ID", std::to_string((uint32_t)entity));
            ImGui::TreePop();
        }
    }
}
