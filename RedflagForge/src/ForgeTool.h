#pragma once

#include <yaml-cpp/yaml.h>

namespace Redflag
{
    // 脚本与编辑器通过一个临时文件交换请求和结果，调用者在后台线程等待。
    YAML::Node RunForgeTool(const char* scriptName, const YAML::Node& request);
} // namespace Redflag
