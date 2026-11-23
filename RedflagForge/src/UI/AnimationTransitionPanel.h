#pragma once
#include "Redflag/Animation/AnimationController.h"
#include <future>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    /* 打开任意角色的动作表，选择两个状态，编辑这一次切换的时长和权重曲线。 */
    class AnimationTransitionPanel
    {
    public:
        void DrawMenu();
        void Draw();

    private:
        void Load(const std::filesystem::path& path);
        void Select();
        void Save(bool remove);
        AnimationController m_Controller;
        AnimationTransition m_Draft;
        std::filesystem::path m_Path;
        std::string m_Message;
        std::future<YAML::Node> m_Save;
        bool m_Open = false;
    };
}
