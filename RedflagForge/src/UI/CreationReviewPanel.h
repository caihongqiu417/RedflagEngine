#pragma once

#include "ReviewImage.h"
#include "AssetChangeReview.h"
#include "PlaytestReview.h"
#include <string>
#include <utility>
#include <filesystem>
#include <imgui.h>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    class VulkanDraw;
    class VulkanContext;

    /* 打开工具写出的创作记录。换案例时，先等上一帧的图片画完，再归还它们。 */
    class CreationReviewPanel
    {
      public:
        void DrawMenu();
        void Draw(VulkanContext& context, VulkanDraw& device);

      private:
        struct Evidence
        {
            std::string Label, File, Text, Error;
            bool Image = false;
            ReviewImage Picture;
        };

        void List();
        void Load(VulkanContext& context, VulkanDraw& device);
        void DrawRecords(VulkanContext& context, VulkanDraw& device);
        void DrawImages();
        void DrawReceipts();

        PlaytestReview m_Playtest;
        AssetChangeReview m_Changes;
        bool m_Startup = true;
        std::string m_Page, m_DeepLink;
        bool m_Open = false, m_Refresh = true;
        std::filesystem::path m_Selected;
        std::vector<std::pair<std::filesystem::path, std::string>> m_Cases;
        YAML::Node m_Record;
        std::vector<Evidence> m_Files;
        std::string m_Error;
        int m_Receipt = 0, m_Zoom = -1;
    };
} // namespace Redflag
