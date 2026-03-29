#pragma once

#include <filesystem>
#include <unordered_map>

#include "Redflag/Renderer/Texture.h"

namespace Redflag {

    class ContentBrowserPanel
    {
    public:
        ContentBrowserPanel();
        void OnImGuiRender();

    private:
        // 当前浏览目录（以 assets 为根）
        std::filesystem::path m_CurrentDirectory;

        // 图标纹理缓存
        Ref<Texture2D> m_DirectoryIcon;   // 文件夹图标
        Ref<Texture2D> m_FileIcon;        // 通用文件图标

        // 以后扩展可按扩展名缓存更多图标（目前暂不使用，但保留字段）
        std::unordered_map<std::string, Ref<Texture2D>> m_FileTypeIcons;

        // 根据路径返回合适的图标（目录 -> m_DirectoryIcon，文件 -> m_FileIcon 或按扩展名）
        Ref<Texture2D> GetIconForPath(const std::filesystem::path& path);
    };

} // namespace Redflag