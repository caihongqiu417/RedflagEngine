#pragma once

// 取 RenderDoc 保存的同一帧来算参考图。相机、几何、灯光和材质保持原样，只有反射方向逐次变化。
static void BuildReflectionReference(ComputeDevice& device, const fs::path& directory)
{
    const fs::path files = directory / "reflection-buffers";
    const auto read = [&](const fs::path& name) {
        std::ifstream stream(files / name, std::ios::binary | std::ios::ate);
        if (!stream || stream.tellg() <= 0) throw std::runtime_error("Missing captured reflection data: " + name.string());
        std::vector<uint8_t> bytes(size_t(stream.tellg()));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        if (!stream) throw std::runtime_error("Could not read " + name.string());
        return bytes;
    };

    static constexpr uint32_t regularShader[] =
#include "Redflag/Renderer/ScreenSpace/Shaders/StochasticSceneSSR.comp.inc"
    ;
    static constexpr uint32_t maskedShader[] =
#include "Redflag/Renderer/ScreenSpace/Shaders/StochasticSceneSSRAlphaMask.comp.inc"
    ;
    unsigned bindingCount = 13; // 早期截帧只保存了 13 个缓冲区。
    if (std::ifstream layout(files / "binding-count.txt"); layout)
        Check(bool(layout >> bindingCount) && (bindingCount == 13 || bindingCount == 14), "Unsupported reflection bindings");
    const bool masked = bindingCount == 14;
    std::vector<Ref<ComputeResource>> buffers;
    std::vector<uint8_t> frame, captured;
    for (uint32_t binding = 0; binding < bindingCount; ++binding)
    {
        std::vector<uint8_t> bytes = read(std::to_string(binding) + ".bin");
        buffers.push_back(device.CreateBuffer(bytes.size(), bytes.data()));
        if (binding == 0) frame = std::move(bytes);
        if (binding == 3) captured = std::move(bytes);
    }
    std::vector<uint8_t> options = read("options.bin");
    Ref<ComputePipeline> pipeline = device.CreatePipeline(masked ? maskedShader : regularShader,
        masked ? sizeof(maskedShader) : sizeof(regularShader),
        std::vector<ComputeBinding>(buffers.size(), ComputeBinding::Buffer), uint32_t(options.size()));
    // Screen.glsl 的 extent 紧跟五个相机矩阵和一个视点，前三项依次为宽、高和帧号。
    constexpr size_t extentOffset = 5 * sizeof(glm::mat4) + sizeof(glm::vec4);
    glm::uvec4 extent;
    Check(frame.size() >= extentOffset + sizeof(extent), "Captured camera buffer is too short");
    std::memcpy(&extent, frame.data() + extentOffset, sizeof(extent));
    const size_t pixels = size_t(extent.x) * extent.y;
    Check(pixels * sizeof(glm::vec4) == captured.size(), "Reference extent differs from the captured output");
    std::vector<glm::vec4> colors(pixels);
    const auto draw = [&]() {
        device.Dispatch(pipeline, buffers, options.data(), (extent.x + 7) / 8, (extent.y + 7) / 8);
        device.Readback(buffers[3], colors.data(), captured.size());
    };

    // 先按截帧中的帧号重算一次。这一步逐字节相同，才能用这个着色器和这些输入计算参照。
    draw();
    Check(std::memcmp(colors.data(), captured.data(), captured.size()) == 0, "Reference dispatch differs from RenderDoc");
    constexpr uint32_t samples = 256;
    std::vector<double> sum[2] = {std::vector<double>(pixels * 3), std::vector<double>(pixels * 3)};
    const uint32_t first = extent.z + 1;
    for (uint32_t sample = 0; sample < samples; ++sample)
    {
        extent.z = first + sample;
        std::memcpy(frame.data() + extentOffset, &extent, sizeof(extent));
        device.Upload(buffers[0], frame.data(), frame.size());
        draw();
        double* dst = sum[sample % 2].data();
        for (const glm::vec4& c : colors)
        {
            *dst++ += c.r;
            *dst++ += c.g;
            *dst++ += c.b;
        }
    }
    // 分开保存奇偶两组，各有 128 个样本。比两张平均图，能估计参考图自己还剩多少噪声。
    for (int group = 0; group < 2; ++group)
    {
        std::vector<float> mean(sum[group].size());
        for (size_t i = 0; i < mean.size(); ++i)
            mean[i] = float(sum[group][i] / (samples / 2));
        std::ofstream out(directory / ("reference-" + std::to_string(group) + ".f32"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(mean.data()), mean.size() * sizeof(float));
        if (!out) throw std::runtime_error("Could not save the averaged reflection");
    }
    std::cout << "256 samples per pixel, frame seeds " << first << " to " << extent.z
              << ", alpha mask " << (masked ? "on" : "off") << "\n";
}
