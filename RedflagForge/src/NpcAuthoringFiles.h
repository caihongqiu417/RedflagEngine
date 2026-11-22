#pragma once

#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    std::filesystem::path NpcTemporaryFile();
    std::filesystem::path ChooseNpcTable(bool save);
    YAML::Node RunNpcAuthoringTool(const YAML::Node& request);
}
