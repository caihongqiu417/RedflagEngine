#pragma once

#include "Redflag/Core/JobSystem.h"
#include <map>
#include <optional>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    /* 任务按游戏取走计时记录的帧归档。重开 Play 后编号可以重复，所以依赖还要核对本局的起始帧。 */
    class JobTimeline
    {
      public:
        void Add(const YAML::Node& row, size_t bytes);
        std::optional<uint64_t> ChooseFrame(uint64_t seq) const;
        void Draw(uint64_t seq, const JobTimeline* reference, uint64_t referenceFrame);

      private:
        struct Task
        {
            uint64_t Run;
            JobTiming Timing;
        };

        struct Sample
        {
            std::vector<Task> Tasks;
            size_t Bytes = 0;
        };

        static std::pair<uint64_t, uint64_t> Range(const Sample& sample);
        void DrawChart(const Sample& sample, uint64_t seq);
        std::map<uint64_t, Sample> m_Frames;
        std::map<std::pair<uint64_t, uint64_t>, uint64_t> m_Locations;
        std::pair<uint64_t, uint64_t> m_Selected{};
    };
}
