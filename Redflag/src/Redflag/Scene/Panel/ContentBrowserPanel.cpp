#include "rfpch.h"
#include "ContentBrowserPanel.h"

#include <imgui.h>
#include <filesystem>

namespace Redflag {

    // 资源根目录（可按需改成项目中的其他路径）
    static std::filesystem::path s_AssetPath = "assets";

    ContentBrowserPanel::ContentBrowserPanel()
        : m_CurrentDirectory(s_AssetPath)
    {
        auto loadOrPlaceholder = [](const std::string& path, uint32_t color) -> Ref<Texture2D> {
            if (std::filesystem::exists(path))
                return Texture2D::Create(path);

            // 回退：创建 1x1 RGBA 纹理并写入单像素颜色
            auto tex = Texture2D::Create(1, 1);
            uint32_t c = color;
            tex->SetData(&c, sizeof(uint32_t)); // 1x1 RGBA => 4 字节
            return tex;
            };

        // 尝试从 assets/icons 加载图标，若不存在使用占位
        m_DirectoryIcon = loadOrPlaceholder("assets/icons/FolderIcon.png", 0xFFAAAAFF);
        m_FileIcon      = loadOrPlaceholder("assets/icons/FileIcon.png",   0xFFFFFFFF);
        m_ImageIcon     = loadOrPlaceholder("assets/icons/ImageIcon.png",  0xFF88FFAA);
        m_SceneIcon     = loadOrPlaceholder("assets/icons/SceneIcon.png",  0xFF88CCFF);
        m_ModelIcon     = loadOrPlaceholder("assets/icons/ModelIcon.png",  0xFFFFCC88);
    }

    Ref<Texture2D> ContentBrowserPanel::GetIconForPath(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path))
            return m_DirectoryIcon;

        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

        // 图片格式
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga" || ext == ".dds")
            return m_ImageIcon;

        // 模型格式
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" ||
            ext == ".glb" || ext == ".dae" || ext == ".3ds")
            return m_ModelIcon;

        // 场景格式
        if (ext == ".redflag")
            return m_SceneIcon;

        // 其余扩展名查专用图标表，最后回退通用图标
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        auto it = m_FileTypeIcons_reserved.find(ext);
        if (it != m_FileTypeIcons_reserved.end())
            return it->second;

        return m_FileIcon;
    }

    void ContentBrowserPanel::OnImGuiRender()
    {
        ImGui::Begin("Content Browser");

        // SourceExtern：在 window 级别维持拖拽，不依赖 item 的 ActiveId
        // 只要鼠标仍按住且有记录路径，每帧持续发布 payload
        if (!m_DragPath.empty())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
                {
                    ImGui::SetDragDropPayload(m_DragType.c_str(), m_DragPath.c_str(), m_DragPath.size() + 1);
                    ImGui::TextUnformatted(m_DragType.c_str());
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", m_DragPath.c_str());
                    ImGui::EndDragDropSource();
                }
            }
            else
            {
                m_DragPath.clear();
                m_DragType.clear();
            }
        }

        // 返回上一级按钮（不在根目录时显示）
        if (m_CurrentDirectory != s_AssetPath)
        {
            if (ImGui::Button("<-"))
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
        }

        // 网格设置（可调整）
        const float padding = 16.0f;             // 单元格间距
        const float thumbnailSize = 64.0f;       // 图标大小
        const float cellSize = thumbnailSize + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        int columnIndex = 0;

        // 遍历当前目录
        for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory))
        {
            // 取得以 assets 为相对基准的显示名称
            auto relativePath = std::filesystem::relative(directoryEntry.path(), s_AssetPath);
            std::string name = relativePath.filename().string();

            // 获取图标（从缓存返回，构造时已加载占位或真实图标）
            Ref<Texture2D> icon = GetIconForPath(directoryEntry.path());

            // 每个项为一个 Group，包含图标和下方的小字体路径文本
            ImGui::BeginGroup();

            // 图标（使用 ImageButton 使其可点击），ImGui 要把 GLuint 转为 void*
            ImGui::PushID(name.c_str()); // 保证同名项的 ImGui ID 唯一
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            bool clicked = ImGui::ImageButton((void*)(intptr_t)icon->GetRendererID(),
                ImVec2(thumbnailSize, thumbnailSize),
                ImVec2(0, 1), ImVec2(1, 0), 0);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            // 鼠标按下时记录路径，由 window 级别的 SourceExtern 负责发布 payload
            std::string fullPath = directoryEntry.path().string();
            std::string ext = directoryEntry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

            bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                ext == ".bmp" || ext == ".tga" || ext == ".dds");
            bool isModel = (ext == ".obj" || ext == ".fbx" || ext == ".gltf" ||
                ext == ".glb" || ext == ".dae" || ext == ".3ds");

            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (isTexture)
                    { m_DragPath = fullPath; m_DragType = "CONTENT_BROWSER_TEXTURE"; }
                else if (isModel)
                    { m_DragPath = fullPath; m_DragType = "CONTENT_BROWSER_MODEL"; }
                else if (ext == ".redflag")
                    { m_DragPath = fullPath; m_DragType = "SCENE_FILE"; }
            }


            // 点击图标进入下一级（如果是目录）
            if (clicked && directoryEntry.is_directory())
            {
                m_CurrentDirectory = m_CurrentDirectory / directoryEntry.path().filename();
            }
            // 对文件点击：保留 TODO（可打开或选中）
            if (clicked && !directoryEntry.is_directory())
            {
                // TODO: 打开文件 / 在侧边栏显示预览 / 选中逻辑
            }

            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbnailSize);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s", name.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();

            ImGui::PopID();
            ImGui::EndGroup();

            // 网格换行逻辑
            columnIndex++;
            if (columnIndex < columnCount)
            {
                ImGui::SameLine();
            }
            else
            {
                columnIndex = 0;
            }
        }

        ImGui::End();
    }

} // namespace Redflag