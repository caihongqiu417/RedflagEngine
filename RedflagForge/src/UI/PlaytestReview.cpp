#include "rfpch.h"
#include "PlaytestReview.h"
#include "../ForgeTool.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Redflag/Debug/PlaytestSession.h"
#include <mmsystem.h>
#include <iomanip>

#pragma comment(lib, "winmm.lib")

namespace Redflag
{
    namespace fs = std::filesystem;

    static YAML::Node Document(const fs::path& file)
    {
        std::ifstream stream(file);
        return stream ? YAML::Load(stream) : YAML::Node();
    }

    template <typename Visit> static void ReadLines(const fs::path& file, std::streamoff& offset, Visit visit)
    {
        std::ifstream stream(file, std::ios::binary);
        stream.seekg(offset);
        std::string line;
        // 留下游戏还没写完的最后一行。一次最多读一批，长回放也能边打开边操作。
        for (int count = 0; count < 600 && std::getline(stream, line) && !stream.eof(); ++count)
        {
            visit(YAML::Load(line), offset);
            offset = stream.tellg();
        }
    }

    static fs::path MediaPath(const fs::path& folder, const std::string& name)
    {
        const fs::path relative = fs::u8path(name);
        const fs::path base = fs::weakly_canonical(folder);
        const fs::path file = fs::weakly_canonical(base / relative);
        const fs::path within = file.lexically_relative(base);
        if (relative.has_root_path() || within.empty() || *within.begin() == "..")
            throw std::runtime_error("媒体文件不在本次试玩目录内。");
        return file;
    }

    // ---- 读取会话和新写入的帧 ----

    void PlaytestReview::Select(const std::string& session)
    {
        m_Requested = session;
        const char* frame = std::getenv("REDFLAG_REVIEW_FRAME");
        const char* baseline = std::getenv("REDFLAG_REVIEW_BASELINE");
        const char* baseFrame = std::getenv("REDFLAG_REVIEW_BASE_FRAME");
        if (frame && *frame) m_RequestedFrame = std::stoull(frame);
        if (baseline) m_RequestedBase = baseline;
        if (baseFrame && *baseFrame) m_RequestedBaseFrame = std::stoull(baseFrame);
        m_NextPoll = 0;
    }

    void PlaytestReview::Open(const fs::path& folder)
    {
        StopAudio();
        m_Folder = folder;
        const YAML::Node session = Document(folder / "session.json");
        m_Intent = session["intent"].as<std::string>();
        m_Scene = session["scene"].as<std::string>();
        m_Track = {};
        m_Reference = {};
        m_ReferenceFolder.clear();
        m_Notes.clear();
        m_Current.reset();
        m_NoteOffset = 0;
        m_Selected = m_Loaded = m_Baseline = -1;
        m_Follow = true;
        m_Playing = false;
        m_Entity.clear();
        m_Comment[0] = 0;
        m_Message.clear();
        m_PendingCommand = 0;
        m_Owner.clear();
        m_State = "ready";
        m_NextPoll = 0;
    }

    YAML::Node PlaytestReview::ReadFrame(int frame) const
    {
        std::ifstream stream(m_Folder / "frames.jsonl", std::ios::binary);
        stream.seekg(m_Track.Frames.at(frame).Offset);
        std::string line;
        std::getline(stream, line);
        return YAML::Load(line);
    }

    void PlaytestReview::Poll()
    {
        const char* project = std::getenv("REDFLAG_PROJECT");
        m_Project = project && *project ? fs::absolute(fs::u8path(project)) : fs::current_path();
        const fs::path root = m_Project / "captures/playtests";
        m_Sessions.clear();
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(root, error))
            if (fs::is_regular_file(entry.path() / "session.json")) m_Sessions.push_back(entry.path());
        std::sort(m_Sessions.rbegin(), m_Sessions.rend());

        if (!m_Requested.empty())
        {
            for (const auto& path : m_Sessions)
                if (path.filename().u8string() == m_Requested)
                {
                    Open(path);
                    break;
                }
            m_Requested.clear();
        }
        if (m_Folder.empty() && !m_Sessions.empty()) Open(m_Sessions.front());
        if (m_Folder.empty()) return;

        const YAML::Node control = Document(m_Folder / "handoff.json");
        const YAML::Node status = Document(m_Folder / "status.json");
        m_Owner = control["owner"].as<std::string>("");
        m_Generation = control["generation"].as<uint64_t>(0);
        m_State = status["state"].as<std::string>("ready");
        ReadTrack(m_Track, m_Folder);
        if (!m_RequestedBase.empty())
        {
            for (const fs::path& folder : m_Sessions)
                if (folder.filename().u8string() == m_RequestedBase)
                {
                    OpenBaseline(folder);
                    break;
                }
            m_RequestedBase.clear();
        }
        if (!m_ReferenceFolder.empty() && m_ReferenceFolder != m_Folder) ReadTrack(m_Reference, m_ReferenceFolder);
        ReadLines(m_Folder / "feedback.jsonl", m_NoteOffset,
                  [&](const YAML::Node& row, std::streamoff) { m_Notes.push_back(row); });
        if (m_Follow && !m_Track.Frames.empty()) m_Selected = int(m_Track.Frames.size()) - 1;
        const auto seek = [](const Track& track, std::optional<uint64_t>& requested, int& index) {
            if (!requested) return false;
            const auto found = std::lower_bound(track.Frames.begin(), track.Frames.end(), *requested,
                                                [](const Frame& frame, uint64_t seq) { return frame.Seq < seq; });
            if (found == track.Frames.end()) return false;
            index = int(found - track.Frames.begin());
            requested.reset();
            return true;
        };
        if (seek(m_Track, m_RequestedFrame, m_Selected)) m_Follow = false;
        seek(m_ReferenceFolder == m_Folder ? m_Track : m_Reference, m_RequestedBaseFrame, m_Baseline);

        if (m_PendingCommand)
        {
            std::ostringstream filename;
            filename << std::setw(6) << std::setfill('0') << m_PendingCommand << ".json";
            const YAML::Node receipt = Document(m_Folder / "receipts" / filename.str());
            if (receipt.IsMap())
            {
                m_Message = "游戏回执 " + std::to_string(m_PendingCommand) + "  " + YAML::Dump(receipt);
                m_PendingCommand = 0;
            }
        }
    }

    void PlaytestReview::ReadTrack(Track& track, const fs::path& folder)
    {
        ReadLines(folder / "frames.jsonl", track.FrameOffset, [&](const YAML::Node& row, std::streamoff offset) {
            const uint64_t seq = row["seq"].as<uint64_t>();
            track.Frames.push_back({seq, row["seconds"].as<double>(), offset});
            if (row["image"] || row["audio"])
                track.Files[seq] = {row["image"].as<std::string>(""), row["audio"].as<std::string>(""), seq};
        });
        // 截图进程读出画面的同时，游戏可能又画了几帧。保留采集前后两个帧号。
        ReadLines(folder / "media.jsonl", track.MediaOffset, [&](const YAML::Node& row, std::streamoff) {
            const uint64_t seq = row["seq"].as<uint64_t>();
            Media& media = track.Files[seq];
            if (row["image"])
            {
                media.File = row["image"].as<std::string>();
                media.LastSeq = row["seq_end"].as<uint64_t>(seq);
            }
            if (row["audio"]) media.Audio = row["audio"].as<std::string>();
            if (row["kind"].as<std::string>("") == "audio") track.VoiceEvents[seq].push_back(row);
        });
    }

    void PlaytestReview::OpenBaseline(const fs::path& folder)
    {
        m_ReferenceFolder = folder;
        m_Reference = {};
        if (folder != m_Folder) ReadTrack(m_Reference, folder);
        const Track& track = folder == m_Folder ? m_Track : m_Reference;
        m_Baseline = track.Frames.empty() ? -1 : folder == m_Folder ? m_Selected : 0;
    }

    // ---- 交接操作与批注 ----

    void PlaytestReview::Submit(const char* action)
    {
        YAML::Node request;
        request["project"] = m_Project.u8string();
        request["session"] = m_Folder.filename().u8string();
        request["action"] = action;
        if (request["action"].Scalar() == "handoff")
        {
            const std::string owner = m_Owner == "human" ? "agent" : "human";
            PlaytestSession* local = PlaytestSession::Active();
            if (local && fs::equivalent(local->Info().Directory, m_Folder))
            {
                m_Message = local->HandOff(owner) ? "游戏已交接操作权。" : "交接失败，请查看当前操作权。";
                m_NextPoll = 0;
                return;
            }
            request["owner"] = owner;
            request["generation"] = m_Generation;
        }
        else
        {
            request["seq"] = m_Track.Frames.at(m_Selected).Seq;
            request["entity"] = m_Entity;
            request["text"] = std::string(m_Comment);
        }
        m_TaskFolder = m_Folder;
        m_Task = std::async(std::launch::async, RunForgeTool, "forge_review.py", request);
    }

    void PlaytestReview::DrawControl()
    {
        const char* state = m_State == "running"                            ? "运行中"
                            : m_State == "stopped"                          ? "已结束"
                            : (m_State == "crashed" || m_State == "exited") ? "异常退出"
                                                                            : "等待启动";
        ImGui::Text("%s    操作权  %s", state, m_Owner == "human" ? "玩家" : m_Owner == "agent" ? "AI" : "待确定");
        if (m_State == "running")
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(m_Task.valid() || m_PendingCommand != 0);
            if (ImGui::Button(m_Owner == "human" ? "交给 AI" : "接管操作")) Submit("handoff");
            ImGui::EndDisabled();
        }
        if (!m_Message.empty()) ImGui::TextWrapped("%s", m_Message.c_str());
    }

    void PlaytestReview::DrawFeedback()
    {
        if (ImGui::BeginCombo("批注对象", m_Entity.empty() ? "整帧" : m_Entity.c_str()))
        {
            if (ImGui::Selectable("整帧", m_Entity.empty())) m_Entity.clear();
            for (const auto& actor : m_Current["actors"])
            {
                const std::string id = actor["id"].as<std::string>("");
                const std::string name = actor["name"].as<std::string>(id);
                if (!id.empty() && ImGui::Selectable((name + "  " + id).c_str(), id == m_Entity)) m_Entity = id;
            }
            ImGui::EndCombo();
        }
        ImGui::InputTextMultiline("##试玩意见", m_Comment, sizeof(m_Comment),
                                  {-1, ImGui::GetTextLineHeightWithSpacing() * 2.5f});
        if (ImGui::IsItemActivated())
        {
            m_Follow = false;
            m_Playing = false;
        }
        ImGui::BeginDisabled(m_Task.valid() || !m_Comment[0]);
        if (ImGui::Button("保存到这一帧")) Submit("feedback");
        ImGui::EndDisabled();
        for (auto note = m_Notes.rbegin(); note != m_Notes.rend(); ++note)
            if ((*note)["seq"].as<uint64_t>() == m_Track.Frames[m_Selected].Seq)
                ImGui::TextWrapped("%s  %s", (*note)["entity"].as<std::string>("").c_str(),
                                   (*note)["text"].as<std::string>().c_str());
    }

    // ---- 时间线、原始状态和画面对照 ----

    void PlaytestReview::DrawTimeline()
    {
        const int last = int(m_Track.Frames.size()) - 1;
        if (ImGui::Checkbox("跟随最新帧", &m_Follow))
        {
            m_Playing = false;
            if (m_Follow) m_Selected = last;
        }
        ImGui::SameLine();
        if (ImGui::Button(m_Playing ? "暂停" : "播放回放"))
        {
            m_Follow = false;
            m_Playing = !m_Playing;
            if (m_Playing)
            {
                if (m_Selected == last) m_Selected = 0;
                m_PlayerClock = ImGui::GetTime() - m_Track.Frames[m_Selected].Seconds;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("设为对照帧")) OpenBaseline(m_Folder);
        if (m_Baseline >= 0)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("清除对照"))
            {
                m_Baseline = -1;
                m_ReferenceFolder.clear();
                m_Reference = {};
            }
        }
        if (m_Playing)
        {
            const double seconds = ImGui::GetTime() - m_PlayerClock;
            while (m_Selected < last && m_Track.Frames[m_Selected + 1].Seconds <= seconds) ++m_Selected;
            if (m_Selected == last) m_Playing = false;
        }
        int item = m_Selected + 1;
        if (ImGui::SliderInt("本局进度", &item, 1, last + 1))
        {
            m_Selected = item - 1;
            m_Playing = false;
            m_Follow = false;
        }
        for (int step : {-1, 1})
        {
            if (step > 0) ImGui::SameLine();
            if (ImGui::SmallButton(step < 0 ? "上一张截图" : "下一张截图"))
                for (int i = m_Selected + step; i >= 0 && i <= last; i += step)
                    if (m_Track.Files.count(m_Track.Frames[i].Seq) && !m_Track.Files.at(m_Track.Frames[i].Seq).File.empty())
                    {
                        m_Selected = i;
                        m_Playing = false;
                        m_Follow = false;
                        break;
                    }
        }
        if (!m_Notes.empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("跳到批注")) ImGui::OpenPopup("批注列表");
            if (ImGui::BeginPopup("批注列表"))
            {
                for (int index = int(m_Notes.size()) - 1; index >= 0; --index)
                {
                    const YAML::Node& note = m_Notes[index];
                    std::string label = "第 " + note["seq"].Scalar() + " 帧   " + note["text"].as<std::string>();
                    if (ImGui::Selectable((label + "##" + std::to_string(index)).c_str()))
                    {
                        m_RequestedFrame = note["seq"].as<uint64_t>();
                        m_Entity = note["entity"].as<std::string>("");
                        m_Follow = m_Playing = false;
                        m_NextPoll = 0;
                    }
                }
                ImGui::EndPopup();
            }
        }
        if (ImGui::BeginCombo("对照试玩", m_ReferenceFolder.empty() ? "选择一局作比较"
                                                                    : m_ReferenceFolder.filename().u8string().c_str()))
        {
            for (const fs::path& folder : m_Sessions)
                if (ImGui::Selectable(folder.filename().u8string().c_str(), folder == m_ReferenceFolder))
                    OpenBaseline(folder);
            ImGui::EndCombo();
        }
        const Track& track = m_ReferenceFolder == m_Folder ? m_Track : m_Reference;
        if (m_Baseline >= 0)
        {
            int base = m_Baseline + 1;
            if (ImGui::SliderInt("对照进度", &base, 1, int(track.Frames.size()))) m_Baseline = base - 1;
            if (ImGui::SmallButton("对齐经过时间"))
            {
                const auto next =
                    std::lower_bound(track.Frames.begin(), track.Frames.end(), m_Track.Frames[m_Selected].Seconds,
                                     [](const Frame& frame, double time) { return frame.Seconds < time; });
                m_Baseline = next == track.Frames.end() ? int(track.Frames.size()) - 1 : int(next - track.Frames.begin());
            }
        }
    }

    void PlaytestReview::DrawPicture(const Track& track, const fs::path& folder, int frame, Picture& cache,
                                     VulkanContext& context, VulkanDraw& device)
    {
        auto source = track.Files.upper_bound(track.Frames.at(frame).Seq);
        while (source != track.Files.begin())
        {
            --source;
            if (!source->second.File.empty()) break;
        }
        if (source == track.Files.end() || source->first > track.Frames[frame].Seq || source->second.File.empty())
        {
            ImGui::TextDisabled("这一段还没有截图。");
            return;
        }
        fs::path path;
        try
        {
            path = MediaPath(folder, source->second.File);
        }
        catch (const std::exception& error)
        {
            ImGui::TextWrapped("%s", error.what());
            return;
        }
        if (path != cache.File)
        {
            if (cache.Image.Texture) context.WaitIdle();
            cache = {};
            cache.File = path;
            try
            {
                std::ifstream stream(path, std::ios::binary);
                if (!stream) throw std::runtime_error("找不到这张截图。");
                cache.Image.Load(device, {std::istreambuf_iterator<char>(stream), {}});
            }
            catch (const std::exception& error)
            {
                cache.Error = error.what();
            }
        }
        if (source->first == source->second.LastSeq)
            ImGui::Text("画面来自第 %llu 帧", (unsigned long long)source->first);
        else
            ImGui::Text("截图采于第 %llu 至 %llu 帧", (unsigned long long)source->first,
                        (unsigned long long)source->second.LastSeq);
        if (!cache.Error.empty())
            ImGui::TextWrapped("%s", cache.Error.c_str());
        else
            cache.Image.Draw(std::min(ImGui::GetContentRegionAvail().x,
                                      ImGui::GetWindowHeight() * .40f * cache.Image.Width / cache.Image.Height));
    }

    void PlaytestReview::StopAudio()
    {
        m_Speech.reset();
        if (m_RawAudio) PlaySoundW(nullptr, nullptr, 0);
        m_RawAudio = false;
    }

    void PlaytestReview::DrawVoice(uint64_t seq)
    {
        if (m_Speech) m_Speech->Update();
        const auto found = m_Track.VoiceEvents.find(seq);
        if (found == m_Track.VoiceEvents.end()) return;
        int index = 0;
        for (const YAML::Node& row : found->second)
        {
            const std::string event = row["event"].as<std::string>();
            const char* label = event=="first_voice" ? "设备首次读到人声" : event=="stopped" ? "中途停止"
                : event=="cancelled" ? "开始播放前取消" : event=="finished" ? "整句播完"
                : event=="submitted" ? "已交给播放设备" : event=="requested" ? "请求语音" : "播放失败";
            ImGui::Text("角色 %s   %s", row["entity"].as<std::string>().c_str(), label);
            const int samples = row["device_samples"].IsScalar() ? row["device_samples"].as<int>() : -1;
            if (samples < 0 || !row["file"].IsScalar()) continue;
            ImGui::TextDisabled("设备已读 %d 个采样，%.3f 秒", samples, double(samples)/row["rate_hz"].as<int>(16000));

            ImGui::PushID(index++);
            ImGui::BeginDisabled(samples==0);
            const bool hear = ImGui::SmallButton("回听已播放部分");
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool full = ImGui::SmallButton("试听合成整句");
            ImGui::SameLine();
            if (ImGui::SmallButton("停止试听")) StopAudio();
            ImGui::PopID();
            if (!hear && !full) continue;
            const fs::path file = MediaPath(m_Folder,row["file"].as<std::string>());
            StopAudio();
            if (full)
            {
                m_RawAudio = PlaySoundW(file.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
                if (!m_RawAudio) m_Message="系统无法播放这份录音。";
            }
            else
            {
                std::ifstream input(file,std::ios::binary);
                if (!input) throw std::runtime_error("找不到提交给播放设备的 WAV。");
                m_Speech = CreateScope<SpeechPlayback>();
                m_Speech->PlayRecording({std::istreambuf_iterator<char>(input),{}},samples);
            }
        }
    }

    void PlaytestReview::DrawFrame(VulkanContext& context, VulkanDraw& device)
    {
        const Frame& frame = m_Track.Frames[m_Selected];
        if (m_Loaded != m_Selected)
        {
            m_Current.reset(ReadFrame(m_Selected));
            m_Loaded = m_Selected;
            if (!m_Entity.empty())
            {
                bool found = false;
                for (const auto& actor : m_Current["actors"]) found |= actor["id"].as<std::string>("") == m_Entity;
                if (!found) m_Entity.clear();
            }
        }
        ImGui::Text("第 %llu 帧    %.2f 秒    %.2f 毫秒/帧", (unsigned long long)frame.Seq, frame.Seconds,
                    m_Current["frame_ms"].as<double>(0));
        if (m_Baseline >= 0)
        {
            const Track& reference = m_ReferenceFolder == m_Folder ? m_Track : m_Reference;
            const Frame& before = reference.Frames[m_Baseline];
            ImGui::Text("对照第 %llu 帧    %.2f 秒", (unsigned long long)before.Seq, before.Seconds);
            if (ImGui::BeginTable("两次画面", 2, ImGuiTableFlags_SizingStretchSame))
            {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("对照局");
                DrawPicture(reference, m_ReferenceFolder, m_Baseline, m_Before, context, device);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("本局");
                DrawPicture(m_Track, m_Folder, m_Selected, m_Now, context, device);
                ImGui::EndTable();
            }
        }
        else
            DrawPicture(m_Track, m_Folder, m_Selected, m_Now, context, device);
        const auto media = m_Track.Files.find(frame.Seq);
        const std::string audio = media == m_Track.Files.end() ? "" : media->second.Audio;
        if (!audio.empty())
        {
            if (ImGui::Button("播放本帧录音"))
            {
                StopAudio();
                m_RawAudio = PlaySoundW(MediaPath(m_Folder, audio).c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
                if (!m_RawAudio) m_Message = "系统无法播放这份录音。";
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("停止录音")) StopAudio();
        }
        DrawVoice(frame.Seq);
        if (ImGui::CollapsingHeader("输入和游戏事件", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto inputs = m_Current["inputs"];
            const auto events = m_Current["events"];
            if (!inputs.size() && !events.size()) ImGui::TextDisabled("这一帧没有记录到操作或事件。");
            for (const auto& item : inputs)
                ImGui::TextWrapped("%s   %s   %s", item["id"].as<std::string>("").c_str(),
                                   item["op"].as<std::string>("").c_str(), item["key"].as<std::string>("").c_str());
            for (const auto& item : events)
                ImGui::TextWrapped("%s   %s   %s", item["entity"].as<std::string>("").c_str(),
                                   item["kind"].as<std::string>("").c_str(), item["text"].as<std::string>("").c_str());
        }
        if (ImGui::CollapsingHeader("本帧批注", ImGuiTreeNodeFlags_DefaultOpen)) DrawFeedback();
        if (ImGui::CollapsingHeader("角色与交互物体"))
        {
            for (const auto& actor : m_Current["actors"])
            {
                const std::string id = actor["id"].as<std::string>();
                const std::string name = actor["name"].as<std::string>(id);
                if (!ImGui::TreeNode((name + "##" + id).c_str())) continue;
                ImGui::TextDisabled("ID  %s", id.c_str());
                const YAML::Node p = actor["position"];
                if (p.size() == 3)
                    ImGui::Text("位置  (%.2f, %.2f, %.2f)", p[0].as<float>(), p[1].as<float>(), p[2].as<float>());
                if (actor["health"]) ImGui::Text("生命值  %.1f", actor["health"].as<float>());
                if (actor["action"])
                    ImGui::TextWrapped("动作  %s   %s", actor["action"].Scalar().c_str(),
                                       actor["stage"].as<std::string>("").c_str());
                for (const auto& item : actor["inventory"])
                    ImGui::TextWrapped("%s × %s", item.first.Scalar().c_str(), item.second.Scalar().c_str());
                ImGui::TreePop();
            }
        }
    }

    void PlaytestReview::Draw(VulkanContext& context, VulkanDraw& device)
    {
        try
        {
            if (m_Task.valid() && m_Task.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                const YAML::Node result = m_Task.get();
                if (m_TaskFolder == m_Folder)
                {
                    if (result["error"])
                        m_Message = result["error"].as<std::string>();
                    else if (result["id"])
                    {
                        m_PendingCommand = result["id"].as<uint64_t>();
                        m_Message = "已提交交接，等待游戏回执。";
                    }
                    else
                    {
                        m_Message = "已保存批注。";
                        m_Comment[0] = 0;
                    }
                }
                m_NextPoll = 0;
            }
            if (ImGui::GetTime() >= m_NextPoll)
            {
                Poll();
                m_NextPoll = ImGui::GetTime() + .25;
            }
            if (m_Sessions.empty())
            {
                ImGui::TextWrapped("还没有试玩记录。从本项目启动一次试玩后，这里会显示它的场景和时间线。");
                return;
            }
            if (ImGui::BeginCombo("试玩记录", m_Folder.filename().u8string().c_str()))
            {
                for (const fs::path& folder : m_Sessions)
                    if (ImGui::Selectable(folder.filename().u8string().c_str(), folder == m_Folder) && folder != m_Folder)
                        Open(folder);
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("%s", m_Intent.c_str());
            ImGui::TextDisabled("%s", m_Scene.c_str());
            DrawControl();
            if (m_Selected >= 0)
            {
                ImGui::Separator();
                DrawTimeline();
                DrawFrame(context, device);
            }
            else
                ImGui::TextDisabled("等待游戏写入第一帧。");
        }
        catch (const std::exception& error)
        {
            ImGui::TextWrapped("无法读取本次试玩：%s", error.what());
        }
    }
} // namespace Redflag
