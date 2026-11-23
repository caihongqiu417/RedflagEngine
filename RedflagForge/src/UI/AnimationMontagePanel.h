#pragma once

#include "Redflag/Animation/Montage.h"

namespace Redflag
{
    class Entity;
    class ModelAnimation;

    /* 作者可独立编辑蒙太奇。选中带骨架的实体时，原片列表取自这个实体的模型。 */
    class AnimationMontagePanel
    {
    public:
        void DrawMenu();
        void Open(const std::filesystem::path& file);
        void Draw(Entity selected);

    private:
        void New(const ModelAnimation* model);
        void Save();
        void DrawClip(const ModelAnimation* model);
        void DrawSections();
        void DrawEvents(const ModelAnimation* model);
        void DrawWarps();

        AnimationMontage m_Draft;
        std::filesystem::path m_Path;
        std::filesystem::file_time_type m_OpenedAt{};
        std::string m_NewFile, m_Message;
        std::string m_SavedClip;
        std::vector<AnimationMarker> m_SavedEvents;
        int m_Preview = -1;
        bool m_PreviewEnd = false;
        bool m_Open = false, m_Editing = false, m_Dirty = false;
    };
}
