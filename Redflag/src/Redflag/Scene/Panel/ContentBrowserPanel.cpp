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
        // 辅助加载：若图标文件存在则载入，否则返回 1x1 纯色占位纹理（避免每帧 IO）
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
        m_DirectoryIcon = loadOrPlaceholder("assets/icons/FolderIcon.png", 0xFFAAAAFF); // 示例颜色
        m_FileIcon = loadOrPlaceholder("assets/icons/FileIcon.png", 0xFFFFFFFF);
    }

    Ref<Texture2D> ContentBrowserPanel::GetIconForPath(const std::filesystem::path& path)
    {
        if (std::filesystem::is_directory(path))
            return m_DirectoryIcon;

        // 可按扩展名查找专用图标（若已填充 m_FileTypeIcons）
        auto ext = path.extension().string();
        if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
        auto it = m_FileTypeIcons.find(ext);
        if (it != m_FileTypeIcons.end())
            return it->second;

        return m_FileIcon;
    }

    void ContentBrowserPanel::OnImGuiRender()
    {
        ImGui::Begin("Content Browser");

        // 返回上一级按钮（不在根目录时显示）
        if (m_CurrentDirectory != s_AssetPath)
        {
            if (ImGui::Button("<-"))
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
        }

        // 网格设置（可调整）
        const float padding = 16.0f;             // 单元格间距
        const float thumbnailSize = 96.0f;       // 图标大小（“很大”的图标）
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
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
            bool clicked = ImGui::ImageButton((void*)(intptr_t)icon->GetRendererID(),
                ImVec2(thumbnailSize, thumbnailSize),
                ImVec2(0, 1), ImVec2(1, 0), 0);
            ImGui::PopStyleVar();

            // ===== 改进版拖拽源（对失去 active 更健壮） =====
            std::string fullPath = directoryEntry.path().string();
            std::string ext = directoryEntry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

            bool isTexture = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                ext == ".bmp" || ext == ".tga" || ext == ".dds");

            if (isTexture && ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
            {
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_TEXTURE", fullPath.c_str(), fullPath.size() + 1);
                    ImGui::TextWrapped("Texture");
                    ImGui::Separator();
                    if (icon && icon->GetRendererID())
                        ImGui::Image((void*)(intptr_t)icon->GetRendererID(), ImVec2(64, 64));
                    ImGui::TextWrapped("%s", fullPath.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            // 场景文件拖拽（保持原样）
            if (ext == ".redflag")
            {
                // 只有当用户在该项上按下并开始拖动时才 Start drag
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        ImGui::SetDragDropPayload("SCENE_FILE", fullPath.c_str(), fullPath.size() + 1);
                        ImGui::Text("Scene");
                        ImGui::Separator();
                        ImGui::TextWrapped("%s", fullPath.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
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

            // 下方显示路径名：视觉上“较小”的表现通过灰色且换行（若需要更小字体需加载额外 ImGui 字体）
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbnailSize); // 限制换行宽度为图标宽度
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1.0f)); // 灰色文本，看起来更纤细
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