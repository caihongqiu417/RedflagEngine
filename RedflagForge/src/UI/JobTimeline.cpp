#include "rfpch.h"
#include "JobTimeline.h"
#include <imgui.h>

namespace Redflag
{
    // ---- 读取采样与按帧定位 ----

    void JobTimeline::Add(const YAML::Node& row, size_t bytes)
    {
        const uint64_t frame = row["seq"].as<uint64_t>(), run = row["run"].as<uint64_t>();
        JobTiming job{};
        job.ID = row["id"].as<uint64_t>();
        job.Name = row["name"].as<std::string>();
        job.Thread = row["thread"].as<uint64_t>();
        job.Lane = row["lane"].as<std::string>() == "main" ? JobThread::Main : JobThread::Worker;
        job.QueuedUs = row["queued_us"].as<uint64_t>();
        job.StartUs = row["start_us"].as<uint64_t>();
        job.EndUs = row["end_us"].as<uint64_t>();
        if (job.QueuedUs > job.StartUs || job.StartUs > job.EndUs) throw std::runtime_error("任务记录的起止时间颠倒。");
        job.File = row["file"].as<std::string>();
        job.Line = row["line"].as<int>();
        job.Failed = row["failed"].as<bool>();
        job.Dependencies = row["depends"].as<std::vector<uint64_t>>();
        Sample& sample = m_Frames[frame];
        m_Locations[{run, job.ID}] = frame;
        const auto at = std::lower_bound(sample.Tasks.begin(), sample.Tasks.end(), job.QueuedUs,
            [](const Task& task, uint64_t time) { return task.Timing.QueuedUs < time; });
        sample.Tasks.insert(at, {run, std::move(job)});
        sample.Bytes += bytes;
    }

    std::pair<uint64_t, uint64_t> JobTimeline::Range(const Sample& sample)
    {
        uint64_t begin = sample.Tasks.front().Timing.QueuedUs, end = begin;
        for (const Task& task : sample.Tasks) end = std::max(end, task.Timing.EndUs);
        return {begin, end};
    }

    std::optional<uint64_t> JobTimeline::ChooseFrame(uint64_t seq) const
    {
        if (m_Frames.empty()) { ImGui::TextDisabled("这次试玩尚无 CPU 任务记录。"); return {}; }
        const std::string value = m_Frames.count(seq) ? "第 " + std::to_string(seq) + " 帧" : "选择有记录的帧";
        std::optional<uint64_t> selected;
        if (ImGui::BeginCombo("采集帧", value.c_str()))
        {
            for (const auto& [frame, sample] : m_Frames)
            {
                const std::string label = "第 " + std::to_string(frame) + " 帧   " + std::to_string(sample.Tasks.size()) + " 项任务";
                if (ImGui::Selectable(label.c_str(), seq == frame)) selected = frame;
            }
            ImGui::EndCombo();
        }
        return selected;
    }

    // ---- 排队、开始执行与依赖 ----

    void JobTimeline::DrawChart(const Sample& sample, uint64_t seq)
    {
        const auto [begin, end] = Range(sample);
        const auto& jobs = sample.Tasks;
        auto selected = std::find_if(jobs.begin(), jobs.end(), [&](const Task& t) {
            return std::make_pair(t.Run, t.Timing.ID) == m_Selected;
        });
        if (selected == jobs.end())
        {
            selected = std::max_element(jobs.begin(), jobs.end(), [](const Task& a, const Task& b) {
                return a.Timing.Dependencies.size() < b.Timing.Dependencies.size();
            });
            m_Selected = {selected->Run, selected->Timing.ID};
        }

        const float em = ImGui::GetFontSize(), rowHeight = em * 1.8f;
        ImGui::BeginChild("任务条", {0, std::min(jobs.size(), size_t(9)) * rowHeight + em * 2.6f}, true);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x, left = pos.x + em * 7.5f;
        const float length = std::max(1.f, width - em * 10.f);
        const float top = pos.y + rowHeight;
        auto x = [&](uint64_t us) { return left + length * float(double(us - begin) / double(std::max<uint64_t>(1, end - begin))); };
        auto y = [&](size_t i) { return top + (float(i) + .5f) * rowHeight; };
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text), muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 worker = IM_COL32(83, 162, 164, 255), main = IM_COL32(240, 174, 100, 255);
        for (int tick = 0; tick <= 4; ++tick)
        {
            const float mark = left + length * tick / 4.f;
            char label[40];
            std::snprintf(label, sizeof(label), "%.2f", (end - begin) * tick / 4000.0);
            draw->AddText({mark - ImGui::CalcTextSize(label).x * .5f, pos.y}, muted, label);
            draw->AddLine({mark, top}, {mark, top + float(jobs.size()) * rowHeight}, IM_COL32(110, 120, 125, 75));
        }
        draw->AddText({left + length + em * .9f, pos.y}, muted, "ms");

        // 每个箭头只连接日志中明确登记的前置任务。时间重叠或先结束本身不能说明存在依赖。
        const float selectedY = y(size_t(selected - jobs.begin()));
        const float headX = x(selected->Timing.StartUs);
        for (uint64_t dep : selected->Timing.Dependencies)
        {
            const auto parent = std::find_if(jobs.begin(), jobs.end(), [&](const Task& item) { return item.Run == selected->Run && item.Timing.ID == dep; });
            if (parent == jobs.end()) continue;
            const float tailX = x(parent->Timing.EndUs), tailY = y(size_t(parent - jobs.begin()));
            const float midY = (selectedY + tailY) * .5f;
            const ImU32 line = IM_COL32(239, 187, 113, 170);
            draw->AddLine({tailX, tailY}, {tailX, midY}, line);
            draw->AddLine({tailX, midY}, {headX, midY}, line);
            draw->AddLine({headX, midY}, {headX, selectedY}, line);
            const float dy = tailY < selectedY ? -1.f : 1.f;
            draw->AddTriangleFilled({headX, selectedY + dy * em * .23f}, {headX - 3, selectedY + dy * em * .55f},
                                    {headX + 3, selectedY + dy * em * .55f}, line);
        }

        std::map<uint64_t, size_t> threads;
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            const Task& task = jobs[i];
            const JobTiming& job = task.Timing;
            const bool onMain = job.Lane == JobThread::Main;
            std::string lane = "主线程";
            if (!onMain)
            {
                const auto index = threads.emplace(job.Thread, threads.size() + 1).first;
                lane = "工作线程 " + std::to_string(index->second);
            }
            const std::string name = "#" + std::to_string(job.ID) + "  " + lane;
            const float cy = y(i), start = x(job.StartUs), finish = x(job.EndUs);
            const ImU32 colour = job.Failed ? IM_COL32(239, 101, 100, 255) : onMain ? main : worker;
            draw->AddText({pos.x, cy - em * .5f}, m_Selected == std::make_pair(task.Run, job.ID) ? main : text, name.c_str());
            draw->AddLine({x(job.QueuedUs), cy}, {start, cy}, muted, 2.f);
            draw->AddCircleFilled({x(job.QueuedUs), cy}, 2.f, muted);
            draw->AddRectFilled({start, cy - em * .55f}, {std::max(start + 2.f, finish), cy + em * .55f}, colour, 2.f);
            char elapsed[48];
            std::snprintf(elapsed, sizeof(elapsed), "%.3f ms", (job.EndUs - job.StartUs) / 1000.);
            const std::string caption = job.Name + "   " + elapsed;
            if (ImGui::CalcTextSize(caption.c_str()).x + em < finish - start)
                draw->AddText({start + em * .4f, cy - em * .5f}, IM_COL32(14, 33, 36, 255), caption.c_str());
            ImGui::SetCursorScreenPos({pos.x, cy - rowHeight * .5f});
            ImGui::PushID(int(i));
            ImGui::InvisibleButton("选择任务", {width, rowHeight});
            if (ImGui::IsItemClicked()) m_Selected = {task.Run, job.ID};
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n线程 %llu   %s:%d", job.Name.c_str(), (unsigned long long)job.Thread, job.File.c_str(), job.Line);
            ImGui::PopID();
        }
        ImGui::EndChild();

        const JobTiming& job = selected->Timing;
        std::string deps;
        for (uint64_t id : job.Dependencies)
        {
            if (!deps.empty()) deps += "、";
            deps += "#" + std::to_string(id);
            const auto at = m_Locations.find({selected->Run, id});
            if (at == m_Locations.end()) deps += "（本局记录中未找到）";
            else if (at->second != seq) deps += "（采于第 " + std::to_string(at->second) + " 帧）";
        }
        ImGui::TextWrapped("#%llu   %s%s", (unsigned long long)job.ID, job.Name.c_str(), job.Failed ? "  执行失败" : "");
        ImGui::TextWrapped("前置任务   %s", deps.empty() ? "无" : deps.c_str());
        ImGui::Text("排队 %.3f ms    执行 %.3f ms", (job.StartUs - job.QueuedUs) / 1000., (job.EndUs - job.StartUs) / 1000.);
        ImGui::TextWrapped("提交位置   %s:%d", job.File.c_str(), job.Line);
    }

    void JobTimeline::Draw(uint64_t seq, const JobTimeline* reference, uint64_t referenceFrame)
    {
        const auto current = m_Frames.find(seq);
        if (current == m_Frames.end()) return;
        const Sample& sample = current->second;
        const auto [begin, end] = Range(sample);
        ImGui::Text("本帧取回 %zu 项任务    从最早提交到最后完成  %.3f ms", sample.Tasks.size(), (end - begin) / 1000.);
        if (reference)
        {
            const auto base = reference->m_Frames.find(referenceFrame);
            if (base == reference->m_Frames.end()) ImGui::TextDisabled("对照帧没有任务样本。");
            else
            {
                const auto [a, b] = Range(base->second);
                ImGui::Text("对照第 %llu 帧    %zu 项任务    %.3f ms", (unsigned long long)referenceFrame,
                            base->second.Tasks.size(), (b - a) / 1000.);
            }
        }
        ImGui::TextWrapped("细线表示排队，色条表示执行。点击一行，箭头会从它等待的任务指向它开始执行的时刻。");
        DrawChart(sample, seq);
        ImGui::TextDisabled("任务日志 %zu 字节。记录和写盘的 CPU 耗时未单独计量。", sample.Bytes);
        ImGui::TextWrapped("多个工作线程可以同时执行。整帧还包括脚本、绘制和其他等待，单项最长耗时无法确定整帧瓶颈。");
    }
}
