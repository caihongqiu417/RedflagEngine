#pragma once

#include "Redflag/Renderer/GI/ProbeBake.h"
#include "Redflag/Utils/FileSystem.h"
#include <future>

static void CheckConcurrentProbeFiles(const std::filesystem::path& folder)
{
    using namespace Redflag;
    std::filesystem::path deep(L"\\\\?\\" + std::filesystem::absolute(folder).wstring());
    while (deep.native().size() < 270)
        deep /= L"probe-file-save-check";
    std::filesystem::create_directories(deep);
    const std::filesystem::path file = deep / "shared.rfprobes";

    // 两种不同的探针同时保存。最后能读出的应是其中完整的一份，包括与该份数据相符的校验值。
    const std::vector<glm::vec4> red(512, glm::vec4(1, 0, 0, 1)), blue(512, glm::vec4(0, 0, 1, 1));
    ProbeBake::Save(file, 0, red);
    auto save = [&](int key, const std::vector<glm::vec4>& values) {
        int completed = 0;
        for (int i = 0; i < 8; ++i)
            try { ProbeBake::Save(file, key, values); ++completed; }
            catch (const std::filesystem::filesystem_error&) {} // Windows 可能拒绝同时替换同一文件。
        return completed;
    };
    auto first = std::async(std::launch::async, [&] { return save(10, red); });
    const int second = save(20, blue);
    const int completed = first.get() + second;
    if (!completed) throw std::runtime_error("No writer saved a probe file");
    std::vector<glm::vec4> saved(red.size());
    if (!(ProbeBake::Load(file, 10, saved) && saved == red) && !(ProbeBake::Load(file, 20, saved) && saved == blue))
        throw std::runtime_error("Concurrent probe saves produced an incomplete file");

    const std::filesystem::path text = deep / "settings.yaml";
    FileSystem::WriteText(text, "SSR: false\n");
    std::ifstream in(text);
    std::string line;
    std::getline(in, line);
    if (line != "SSR: false") throw std::runtime_error("Lost text at a long Windows path");
    for (const auto& entry : std::filesystem::directory_iterator(deep))
        if (entry.path().extension() == ".tmp") throw std::runtime_error("Temporary file left in scene directory");
    std::cout << "Concurrent probe writes: " << completed << "/16 completed, file verified. Long Windows path: "
              << file.native().size() << " characters.\n";
}

static void CheckProbeSaveFailure(Redflag::VulkanComputeDevice& device, const std::filesystem::path& folder)
{
    using namespace Redflag;
    const std::filesystem::path blocked = folder / "directory.rfprobes";
    std::filesystem::create_directories(blocked);
    CheckConcurrentProbeFiles(folder);
    bool failed = false;
    for (bool feedback : { false, true })
        for (bool baked : { false, true })
        {
            GlobalIllumination gi(device, std::vector<GIModel>{ GIModel{} }, 8, 8);
            gi.SetLights({ {{2, 2, 2, 1}, {18, 16, 12, 0}} });
            gi.SetProbeFeedback(feedback);
            if (baked) gi.BakeProbes(folder / "saved.rfprobes");
            gi.Render(GIMethod::DDGI, 1);

            // 每个探针的六个方向各存两个 vec4。保存失败前后，逐字节比较显卡正在使用的整张表。
            const size_t bytes = 12 * 6 * 7 * 6 * 2 * sizeof(glm::vec4);
            std::vector<uint8_t> before(bytes), after(bytes);
            const Ref<ComputeResource> original = gi.GetReflectionResources().Probes;
            device.Readback(original, before.data(), bytes);
            const uint32_t frame = gi.GetFrameIndex();
            try
            {
                gi.BakeProbes(blocked);
                throw std::runtime_error("Replaced a directory with a probe file");
            }
            catch (const std::filesystem::filesystem_error&) {}
            const Ref<ComputeResource> current = gi.GetReflectionResources().Probes;
            device.Readback(current, after.data(), bytes);
            size_t changed = 0;
            for (size_t i = 0; i < bytes; ++i)
                if (before[i] != after[i]) ++changed;
            std::cout << "feedback=" << feedback << " baked=" << baked << " changed_bytes=" << changed
                      << " same_buffer=" << (current == original) << '\n';
            failed |= changed != 0 || current != original || frame != gi.GetFrameIndex() || baked != gi.HasBakedProbes();
        }
    if (failed) throw std::runtime_error("Failed bake changed the probes used to light the scene");
}

