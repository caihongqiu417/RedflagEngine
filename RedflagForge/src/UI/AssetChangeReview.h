#pragma once

#include <filesystem>
#include <future>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    class AssetChangeReview
    {
      public:
        void Select(const std::string& id);
        void Draw();

      private:
        struct Item
        {
            std::filesystem::path File;
            std::string Intent;
        };

        void Refresh();
        std::vector<Item> m_Items;
        std::filesystem::path m_Project;
        std::string m_ID, m_Message;
        YAML::Node m_Record;
        std::future<YAML::Node> m_Task;
        double m_NextPoll = 0;
    };
} // namespace Redflag
