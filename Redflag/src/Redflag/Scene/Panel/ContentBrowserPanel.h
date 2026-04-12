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
        // ��ǰ���Ŀ¼���� assets Ϊ����
        std::filesystem::path m_CurrentDirectory;

        // ͼ����������
        Ref<Texture2D> m_DirectoryIcon;   // 文件夹图标
        Ref<Texture2D> m_FileIcon;        // 通用文件图标
        Ref<Texture2D> m_ImageIcon;       // 图片文件图标
        Ref<Texture2D> m_SceneIcon;       // 场景文件图标
        Ref<Texture2D> m_ModelIcon;       // 模型文件图标

        // 以后可扩展成按扩展名存储专用图标（目前暂不使用，保留此字段）
        std::unordered_map<std::string, Ref<Texture2D>> m_FileTypeIcons_reserved;

        // 加载路径并返回合适图标（目录 -> m_DirectoryIcon，文件 -> 按扩展名）
        Ref<Texture2D> GetIconForPath(const std::filesystem::path& path);

        // 跨窗口拖拽状态（SourceExtern 方案）
        std::string m_DragPath;
        std::string m_DragType;
    };

} // namespace Redflag
