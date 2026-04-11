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
        Ref<Texture2D> m_DirectoryIcon;   // �ļ���ͼ��
        Ref<Texture2D> m_FileIcon;        // ͨ���ļ�ͼ��

        // �Ժ���չ�ɰ���չ���������ͼ�꣨Ŀǰ�ݲ�ʹ�ã��������ֶΣ�
        std::unordered_map<std::string, Ref<Texture2D>> m_FileTypeIcons;

        // ����·�����غ��ʵ�ͼ�꣨Ŀ¼ -> m_DirectoryIcon���ļ� -> m_FileIcon ����չ����
        Ref<Texture2D> GetIconForPath(const std::filesystem::path& path);

        // 跨窗口拖拽状态（SourceExtern 方案）
        std::string m_DragPath;
        std::string m_DragType;
    };

} // namespace Redflag