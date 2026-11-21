#pragma once

#include <glad/glad.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

/* 对同一画面的几种画法轮换采样。CPU 计提交，显卡计第一条至最后一条命令的时间。 */
template<size_t N, typename DrawFrame>
void MeasureDraws(const std::filesystem::path& csvPath, const std::array<const char*, N>& names, DrawFrame drawFrame)
{
    constexpr size_t warmupRounds = 30;
    constexpr size_t sampleRounds = 120;
    using Clock = std::chrono::steady_clock;

    struct Sample
    {
        size_t Round, Mode;
        long long CpuNs;
        GLuint64 GpuNs;
    };

    GLint timerBits = 0;
    glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &timerBits);
    if (timerBits == 0)
        throw std::runtime_error("GL_TIME_ELAPSED is unavailable");

    std::vector<Sample> samples;
    samples.reserve(sampleRounds * N);
    GLuint timer = 0;
    glGenQueries(1, &timer);
    glFinish();
    for (size_t round = 0; round < warmupRounds + sampleRounds; ++round)
    {
        for (size_t slot = 0; slot < N; ++slot)
        {
            // 每轮换一种画法先执行，两种画法时就是 AB、BA 交替。
            const size_t mode = (round + slot) % N;
            glBeginQuery(GL_TIME_ELAPSED, timer);
            const auto start = Clock::now();
            drawFrame(mode);
            const auto stop = Clock::now();
            glEndQuery(GL_TIME_ELAPSED);

            // 在计时区间外等本帧画完，下一次提交从空队列开始。
            GLuint64 gpuNs = 0;
            glGetQueryObjectui64v(timer, GL_QUERY_RESULT, &gpuNs);
            if (round >= warmupRounds)
            {
                const auto cpuNs = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
                samples.push_back({ round - warmupRounds + 1, mode, cpuNs, gpuNs });
            }
        }
    }
    glDeleteQueries(1, &timer);

    // 采样结束后统一写盘，原始值保留纳秒。
    std::ofstream csv;
    csv.exceptions(std::ios::failbit | std::ios::badbit);
    csv.open(csvPath);
    csv << "round,mode,cpu_ns,gpu_ns\n";
    for (const auto& sample : samples)
        csv << sample.Round << ',' << names[sample.Mode] << ',' << sample.CpuNs << ',' << sample.GpuNs << '\n';
    csv.close();
}
