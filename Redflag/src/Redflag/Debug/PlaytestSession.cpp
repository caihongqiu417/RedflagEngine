#include "rfpch.h"
#include "PlaytestSession.h"
#include "Redflag/Animation/MontagePlayer.h"
#include "Redflag/Gameplay/HandContact.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Events/KeyEvent.h"
#include "Redflag/Events/MouseEvent.h"
#include "Redflag/Scene/Entity.h"
#include <imgui.h>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
    namespace
    {
        PlaytestSession *s_Active = nullptr;

        void Json(YAML::Emitter &out)
        {
            out.SetStringFormat(YAML::DoubleQuoted);
            out.SetMapFormat(YAML::Flow);
            out.SetSeqFormat(YAML::Flow);
        }

        YAML::Node Read(const std::filesystem::path &path)
        {
            std::ifstream in(path);
            return in ? YAML::Load(in) : YAML::Node();
        }

        void Replace(const std::filesystem::path &path, const YAML::Emitter &json)
        {
            std::filesystem::path temp = path;
            temp += L".tmp";
            {
                std::ofstream out(temp, std::ios::binary);
                out << json.c_str() << '\n';
                out.close();
                if (!out) throw std::runtime_error("Cannot write " + temp.u8string());
            }
            for (int attempt=0; attempt<8; ++attempt) {
                if (MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)) return;
                if (GetLastError()!=ERROR_SHARING_VIOLATION) break;
                Sleep(1); // 读者关闭旧文件后，原子替换才会成功。
            }
            throw std::runtime_error("Cannot publish " + path.u8string());
        }

        std::string Ticket(unsigned id)
        {
            std::ostringstream name;
            name << std::setw(6) << std::setfill('0') << id << ".json";
            return name.str();
        }

        unsigned TicketNumber(const std::filesystem::path &path)
        {
            const std::string name = path.filename().string();
            if (name.size() != 11 || name.substr(6) != ".json" ||
                name.substr(0, 6).find_first_not_of("0123456789") != std::string::npos)
                return 0;
            return unsigned(std::stoul(name));
        }

        YAML::Node LastFrame(const std::filesystem::path &path)
        {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in || in.tellg() <= 0) return {};
            // 上一次可能被强制结束，只使用最后一条已写完并换行的记录。
            const std::streamoff size = in.tellg();
            std::streamoff pos = size - 1, end = -1;
            for (; pos >= 0; --pos)
            {
                in.seekg(pos);
                if (in.get() != '\n') continue;
                if (end >= 0) break;
                end = pos;
            }
            in.seekg(pos + 1);
            std::string line;
            if (end >= 0) std::getline(in, line);
            in.close();
            if (end + 1 < size) std::filesystem::resize_file(path, end + 1);
            return line.empty() ? YAML::Node() : YAML::Load(line);
        }
    } // namespace

    // ---- 播放开始与结束 ----
    std::unique_ptr<PlaytestSession> PlaytestSession::Open(const std::filesystem::path &scene)
    {
        const wchar_t *configured = _wgetenv(L"REDFLAG_PLAYTEST");
        if (!configured || !*configured) return {};
        std::unique_ptr<PlaytestSession> session(new PlaytestSession);
        PlaytestInfo &info = session->m_Info;
        info.Directory = std::filesystem::absolute(configured);
        info.Scene = scene.generic_u8string();
        const auto &dir = info.Directory;
        try
        {
            std::filesystem::create_directories(dir / "commands");
            std::filesystem::create_directories(dir / "receipts");
            HANDLE lock = CreateFileW((dir / ".runtime-lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (lock == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Playtest session already in use: " + dir.u8string());
            session->m_Writer = lock;
            const YAML::Node config = Read(dir / "session.json"), owner = Read(dir / "handoff.json"),
                             status = Read(dir / "status.json");
            info.Intent = config["intent"].as<std::string>("");
            if (config["pose_nodes"])
                session->m_PoseNodes = config["pose_nodes"].as<std::vector<std::string>>();
            info.Owner = owner["owner"].as<std::string>("agent");
            if (info.Owner != "human" && info.Owner != "agent") throw std::runtime_error("Invalid playtest owner");
            info.Generation = owner["generation"].as<uint64_t>(1);
            info.Seq = status["seq"].as<uint64_t>(0);
            info.Seconds = status["seconds"].as<double>(0);
            if (const YAML::Node last = LastFrame(dir / "frames.jsonl"); last.IsMap())
            {
                info.Seq = last["seq"].as<uint64_t>();
                info.Seconds = last["seconds"].as<double>();
                ++info.Generation;
            }
            for (const auto &entry : std::filesystem::directory_iterator(dir / "receipts"))
            {
                session->m_LastCommand = std::max(session->m_LastCommand, TicketNumber(entry.path()));
            }
            session->m_Frames.open(dir / "frames.jsonl", std::ios::binary | std::ios::app);
            if (!session->m_Frames) throw std::runtime_error("Cannot open playtest frames");
            const char *trace = std::getenv("REDFLAG_TRACE_DIALOGUE");
            if (trace && std::string(trace) == "1")
            {
                session->m_Dialogue.open(dir / "dialogue-trace.jsonl", std::ios::binary | std::ios::app);
                if (!session->m_Dialogue) throw std::runtime_error("Cannot open playtest dialogue trace");
            }
            session->SaveStatus("running");
            session->SaveOwner();
            session->Event(0, "play", "start");
            if (Application* app = Application::Current())
            {
                session->m_Jobs = &app->GetJobs();
                session->m_JobsFrom = info.Seq + 1;
                session->m_Jobs->Trace(true);
            }
            s_Active = session.get();
            return session;
        }
        catch (const std::exception &error)
        {
            RF_CORE_ERROR("Playtest: {0}", error.what());
            return {};
        }
    }

    PlaytestSession *PlaytestSession::Active()
    {
        return s_Active;
    }

    PlaytestSession::~PlaytestSession()
    {
        if (s_Active == this)
        {
            SaveJobs();
            if (m_Jobs) m_Jobs->Trace(false);
            s_Active = nullptr;
            m_Input.Clear();
            if (ImGui::GetCurrentContext()) m_Input.SendToGui(false);
            try
            {
                SaveStatus("stopped");
            }
            catch (const std::exception &error)
            {
                RF_CORE_ERROR("Playtest: {0}", error.what());
            }
        }
        if (m_Writer) CloseHandle(m_Writer);
    }

    void PlaytestSession::SaveStatus(const char *state)
    {
        YAML::Emitter out;
        Json(out);
        out << YAML::BeginMap << "state" << state << "seq" << m_Info.Seq << "seconds" << m_Info.Seconds << "scene"
            << m_Info.Scene << "pid" << GetCurrentProcessId() << YAML::EndMap;
        Replace(m_Info.Directory / "status.json", out);
    }

    void PlaytestSession::SaveOwner()
    {
        YAML::Emitter out;
        Json(out);
        out << YAML::BeginMap << "owner" << m_Info.Owner << "generation" << m_Info.Generation << YAML::EndMap;
        Replace(m_Info.Directory / "handoff.json", out);
    }

    // ---- 本帧命令与控制权 ----
    bool PlaytestSession::HandOff(const std::string &owner)
    {
        if (owner != "human" && owner != "agent") return false;
        if (m_Info.Owner == owner) return true;
        m_Input.Clear();
        if (ImGui::GetCurrentContext()) Application::Get().GetImGuiLayer()->ReleaseGameMouse();
        m_Info.Owner = owner;
        ++m_Info.Generation;
        SaveOwner();
        Event(0, "handoff", owner);
        return true;
    }

    void PlaytestSession::BeginFrame(double seconds)
    {
        ++m_Info.Seq;
        m_Info.FrameMs = seconds * 1000.0;
        m_Info.Seconds += seconds;
        for (const std::string &key : m_Input.BeginFrame())
        {
            m_Inputs.push_back({"agent", "release", key});
            Event(0, "lease_expired", key);
        }
        Command();
    }

    void PlaytestSession::Command()
    {
        unsigned id = m_LastCommand + 1;
        const auto dir = m_Info.Directory / "commands";
        if (!std::filesystem::exists(dir / Ticket(id)))
        {
            // 写命令的进程可能刚分到编号就退出。目录变化时再查下一张已发布的票。
            const auto updated = std::filesystem::last_write_time(dir);
            if (m_CommandDirectoryTime == updated) return;
            m_CommandDirectoryTime = updated;
            unsigned next = 0;
            for (const auto &entry : std::filesystem::directory_iterator(dir))
            {
                const unsigned found = TicketNumber(entry.path());
                if (found > m_LastCommand && (!next || found < next)) next = found;
            }
            if (!next) return;
            id = next;
        }
        const std::string file = Ticket(id);
        std::string reason;
        std::string detail;
        bool end = false;
        std::ifstream input(dir/file);
        if (!input) return; // 文件仍由发布者占用，下一帧再读同一条命令。
        try
        {
            const YAML::Node cmd = YAML::Load(input);
            const std::string op = cmd["op"].as<std::string>();
            if (cmd["id"].as<unsigned>() != id)
                reason = "id_mismatch";
            else if (cmd["generation"].as<uint64_t>() != m_Info.Generation)
                reason = "stale_generation";
            else if (op == "handoff")
            {
                if (!HandOff(cmd["owner"].as<std::string>())) reason = "unknown_owner";
            }
            else if (m_Info.Owner != "agent")
                reason = "human_controls_game";
            else if (op == "end")
                end = true;
            else
            {
                const std::string key = cmd["key"].as<std::string>("");
                std::optional<std::pair<float, float>> point;
                if (cmd["x"] || cmd["y"])
                {
                    float x = cmd["x"].as<float>(), y = cmd["y"].as<float>();
                    if (!std::isfinite(x) || !std::isfinite(y)) throw std::runtime_error("invalid_position");
                    point = std::make_pair(x, y);
                }
                // 未续期的按键 3.5 秒后松开。长按可传毫秒数或在到期前再次发 hold。
                reason = m_Input.Apply(op, key, point, cmd["lease_ms"].as<int>(3500));
                if (reason.empty()) m_Inputs.push_back({"agent", op, key, id, point});
            }
        }
        catch (const std::exception &error)
        {
            reason = "invalid_command";
            detail = error.what();
        }
        YAML::Emitter ack;
        Json(ack);
        ack << YAML::BeginMap << "id" << id << "generation" << m_Info.Generation << "seq" << m_Info.Seq << "ok"
            << reason.empty() << "reason" << reason;
        if (!detail.empty()) ack << "detail" << detail;
        ack << YAML::EndMap;
        Replace(m_Info.Directory / "receipts" / file, ack);
        m_LastCommand = id;
        if (end) Application::Get().Close();
    }

    void PlaytestSession::SendToGui()
    {
        m_Input.SendToGui(m_Info.Owner == "agent");
    }

    // ---- 游戏状态、日志与回看批注 ----
    void PlaytestSession::HumanInput(const Redflag::Event &event)
    {
        if (m_Info.Owner != "human") return;
        const EventType type = event.GetEventType();
        if (type == EventType::MouseMoved)
        {
            const auto &motion = static_cast<const MouseMovedEvent &>(event);
            m_HumanPointer = std::make_pair(motion.GetX(), motion.GetY());
        }
        else if (type == EventType::KeyPressed || type == EventType::KeyReleased)
        {
            const auto &key = static_cast<const KeyEvent &>(event);
            if (type == EventType::KeyPressed && static_cast<const KeyPressedEvent &>(event).GetRepeatCount()) return;
            const int code = key.GetKeyCode();
            const std::string name = (code >= 'A' && code <= 'Z') ? std::string(1, char(code)) : std::to_string(code);
            m_Inputs.push_back({"human", type == EventType::KeyPressed ? "hold" : "release", name});
        }
        else if (type == EventType::MouseButtonPressed || type == EventType::MouseButtonReleased)
        {
            const int button = static_cast<const MouseButtonEvent &>(event).GetMouseButton();
            const char *names[] = {"MouseLeft", "MouseRight", "MouseMiddle"};
            if (button >= 0 && button < 3)
                m_Inputs.push_back(
                    {"human", type == EventType::MouseButtonPressed ? "hold" : "release", names[button]});
        }
    }

    void PlaytestSession::Event(uint64_t entity, const std::string &kind, const std::string &text)
    {
        m_Events.push_back({entity ? std::to_string(entity) : "", kind, text});
    }

    void PlaytestSession::Note(uint64_t seq, const std::string &entity, const std::string &text)
    {
        YAML::Emitter out;
        Json(out);
        out << YAML::BeginMap << "seq" << seq << "entity" << entity << "text" << text << YAML::EndMap;
        std::string line = out.c_str();
        line += '\n';
        HANDLE file = CreateFileW((m_Info.Directory / "feedback.jsonl").c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open playtest feedback");
        DWORD count = 0;
        const bool written = WriteFile(file, line.data(), DWORD(line.size()), &count, nullptr) && count == line.size();
        CloseHandle(file);
        if (!written) throw std::runtime_error("Cannot write playtest feedback");
    }

    void PlaytestSession::SaveJobs()
    {
        if (!m_Jobs) return;
        const std::vector<JobTiming> jobs = m_Jobs->TakeTimings();
        if (jobs.empty()) return;
        std::ofstream log(m_Info.Directory / "jobs.jsonl", std::ios::binary | std::ios::app);
        for (const JobTiming& job : jobs)
        {
            YAML::Emitter out;
            Json(out);
            out << YAML::BeginMap << "seq" << m_Info.Seq << "run" << m_JobsFrom << "id" << job.ID << "name" << job.Name
                << "lane" << (job.Lane == JobThread::Main ? "main" : "worker") << "thread" << std::to_string(job.Thread)
                << "queued_us" << job.QueuedUs << "start_us" << job.StartUs << "end_us" << job.EndUs
                << "file" << job.File << "line" << job.Line << "failed" << job.Failed << "depends" << YAML::BeginSeq;
            for (uint64_t dependency : job.Dependencies) out << dependency;
            out << YAML::EndSeq << YAML::EndMap;
            log << out.c_str() << '\n';
        }
    }

    void PlaytestSession::RecordDialogue(uint64_t actor, const NpcState &npc)
    {
        // Forge 的请求面板也读取 Decisions。模型答完或请求结束时才向文件写一行。
        uint64_t &last = m_LastDialogue[actor];
        for (const NpcDecisionTrace &request : npc.Decisions)
        {
            if (request.ID <= last || (request.Kind != "对话意图" && request.Kind != "角色答话")) continue;
            if (request.Status == "等待回答") break;

            YAML::Emitter out;
            Json(out);
            out << YAML::BeginMap << "generation" << m_Info.Generation << "seq" << m_Info.Seq
                << "seconds" << m_Info.Seconds << "entity" << std::to_string(actor) << "id" << request.ID
                << "kind" << request.Kind << "status" << request.Status << "choice" << request.Choice
                << "outcome" << request.Outcome << "model_input" << request.ModelInput
                << "model_output" << request.ModelOutput << YAML::EndMap;
            m_Dialogue << out.c_str() << '\n';
            m_Dialogue.flush();
            last = request.ID;
        }
    }

    void PlaytestSession::RecordScene(Scene &scene)
    {
        SaveJobs();
        YAML::Emitter out;
        Json(out);
        out << YAML::BeginMap << "seq" << m_Info.Seq << "seconds" << m_Info.Seconds << "frame_ms" << m_Info.FrameMs
            << "owner" << m_Info.Owner << "generation" << m_Info.Generation << "actors" << YAML::BeginSeq;
        auto &registry = scene.GetRegistry();
        for (entt::entity handle : registry.view<IDComponent, TagComponent, TransformComponent>())
        {
            // 记录会动或可交互的物体。静态背景的网格和贴图可以从场景文件查到。
            if (!registry.any_of<ScriptComponent, NpcComponent, HealthComponent, PickupComponent, InteractionComponent,
                                 CameraComponent, RigidBody2DComponent, RigidBody3DComponent>(handle))
                continue;
            const uint64_t id = registry.get<IDComponent>(handle).ID;
            const auto &transform = registry.get<TransformComponent>(handle);
            const glm::vec3 p = transform.Translation;
            out << YAML::BeginMap << "id" << std::to_string(id) << "name" << registry.get<TagComponent>(handle).Tag
                << "position" << YAML::BeginSeq << p.x << p.y << p.z << YAML::EndSeq;
            if (const auto *health = registry.try_get<HealthComponent>(handle)) out << "health" << health->Current;
            if (!m_PoseNodes.empty())
            {
                if (const auto *animation = registry.try_get<ModelAnimationComponent>(handle))
                {
                    // 读取贴地、混合之后的姿势。世界坐标让录像能区分角色向前走和脚掌在地上滑。
                    out << "animation" << YAML::BeginMap << "clip" << animation->Clip << "time" << animation->Time
                        << "yaw" << transform.Rotation.y << "nodes" << YAML::BeginMap;
                    const glm::mat4 model = transform.GetTransform();
                    for (const std::string &name : m_PoseNodes)
                    {
                        glm::mat4 joint;
                        if (!animation->GetNodeTransform(name, joint)) continue;
                        const glm::vec3 point(model * joint[3]);
                        out << name << YAML::BeginSeq << point.x << point.y << point.z << YAML::EndSeq;
                    }
                    out << YAML::EndMap;
                    if (const MontagePlayer* montage = registry.try_get<MontagePlayer>(handle)) {
                        out << "montages" << YAML::BeginSeq;
                        for (const MontageLayerSample& layer : montage->Samples())
                            out << YAML::BeginMap << "slot" << layer.Slot << "clip" << layer.Clip << "state" << layer.State
                                << "section" << layer.Section << "time" << layer.Time << "weight" << layer.Weight
                                << "fading" << layer.Fading << YAML::EndMap;
                        out << YAML::EndSeq;
                    }
                    out << YAML::EndMap;
                }
            }
            if (const auto* hand = registry.try_get<HandContact>(handle)) {
                Entity owner(handle,&scene);
                glm::vec3 grip;
                if (ReadContactSocket(owner,"grip",grip)) {
                    const glm::vec3 goal = hand->Goal;
                    out << "contact" << YAML::BeginMap << "action" << hand->Action << "target" << std::to_string(hand->Target)
                        << "hand" << YAML::BeginSeq << grip.x << grip.y << grip.z << YAML::EndSeq
                        << "goal" << YAML::BeginSeq << goal.x << goal.y << goal.z << YAML::EndSeq
                        << "distance" << glm::distance(grip,goal) << "prop" << std::to_string(hand->Prop)
                        << "passed" << hand->Passed << "released" << hand->Released;
                    if (hand->Passed) {
                        const auto& h=hand->CommitHand; const auto& g=hand->CommitGoal;
                        out << "commit_hand" << YAML::BeginSeq << h.x << h.y << h.z << YAML::EndSeq
                            << "commit_goal" << YAML::BeginSeq << g.x << g.y << g.z << YAML::EndSeq
                            << "commit_distance" << glm::distance(h,g);
                    }
                    out << YAML::EndMap;
                }
            }
            if (const auto *inventory = registry.try_get<InventoryComponent>(handle))
            {
                out << "inventory" << YAML::BeginMap;
                for (const auto &item : inventory->Items)
                    out << item.first << item.second;
                out << YAML::EndMap;
            }
            if (registry.any_of<NpcComponent>(handle) && scene.GetNpcs())
            {
                if (const NpcState *npc = scene.GetNpcs()->Inspect(id))
                {
                    out << "action" << npc->Action << "stage" << npc->Stage;
                    if (CapturesDialogue()) RecordDialogue(id, *npc);
                    uint64_t &last = m_NpcEvents[id];
                    for (const NpcEvent &event : npc->Events)
                        if (event.Sequence > last)
                        {
                            Event(id, event.Kind, event.Text);
                            last = event.Sequence;
                        }
                }
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq << "events" << YAML::BeginSeq;
        for (const Entry &item : m_Events)
            out << YAML::BeginMap << "entity" << item.Entity << "kind" << item.Kind << "text" << item.Text
                << YAML::EndMap;
        out << YAML::EndSeq << "inputs" << YAML::BeginSeq;
        if (m_HumanPointer) m_Inputs.push_back({"human", "move", "", 0, m_HumanPointer});
        for (const InputSample &item : m_Inputs)
        {
            out << YAML::BeginMap << "owner" << item.Owner << "op" << item.Op << "key" << item.Key;
            if (item.Command) out << "id" << item.Command;
            if (item.Position) out << "x" << item.Position->first << "y" << item.Position->second;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;
        m_Frames << out.c_str() << '\n';
        m_Frames.flush();
        m_Events.clear();
        m_Inputs.clear();
        m_HumanPointer.reset();
    }
} // namespace Redflag
