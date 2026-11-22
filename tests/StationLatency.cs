using System;
using System.Globalization;
using System.IO;
using Redflag;

namespace StationTest
{
    /* 运行在维修站的副本中。用主机时钟量玩家发令到游戏接收动作的时间。 */
    public class Latency : Entity
    {
        public Entity Technician;
        public int Count;
        public bool Moving;
        public bool DispatchOnly;
        private readonly long epoch = DateTime.UtcNow.Ticks;
        private double Milliseconds { get { return (DateTime.UtcNow.Ticks - epoch) / 10000.0; } }
        private double started, next = 2500, choice;
        private ulong cursor;
        private int trial, updates;
        private Vector3 origin;
        private string result;

        private void Finish(string status)
        {
            File.AppendAllText("latency.tsv", string.Format(CultureInfo.InvariantCulture,
                "{0}\t{1}\t{2}\t{3:F3}\t{4:F3}\t{5}\n", trial, status, result, choice,
                Milliseconds - started, updates));
            Npc.Cancel(Technician.Id);
            if (Moving) Technician.GetComponent<TransformComponent>().Translation = origin;
            started = 0;
            next = Milliseconds + 250;
        }

        void OnUpdate(float seconds)
        {
            NpcEvent entry;
            if (started == 0)
            {
                if (trial >= Count || Milliseconds < next) return;
                while (Npc.TryReadEvent(Technician.Id, ref cursor, out entry)) {}
                origin = Technician.GetComponent<TransformComponent>().Translation;
                result = "";
                choice = -1;
                updates = 0;
                ++trial;
                started = Milliseconds;
                if (!Npc.Submit(Technician.Id, Moving ? "带着医疗包，马上走到我身边把它给我。" : "帮我留一份医疗包，等我过来再给我。"))
                    Finish("submit_failed");
                else if (DispatchOnly)
                {
                    // 两个时刻均取自主机事件。到“提问”时请求已交给后台，尚未等模型回答。
                    double recorded = -1, queued = -1;
                    while (Npc.TryReadEvent(Technician.Id, ref cursor, out entry))
                    {
                        if (entry.Kind == "玩家") recorded = entry.Time;
                        if (entry.Kind == "提问") queued = entry.Time;
                    }
                    choice = 1000 * (queued - recorded);
                    result = "player_to_request";
                    Finish(recorded >= 0 && queued >= recorded ? "queued" : "missing_event");
                }
                return;
            }

            ++updates;
            bool waiting = false;
            while (Npc.TryReadEvent(Technician.Id, ref cursor, out entry))
            {
                if (entry.Kind == "选择" && choice < 0)
                {
                    choice = Milliseconds - started;
                    result = entry.Text;
                }
                if (entry.Kind == "动作" && entry.Text == "wait") waiting = true;
                if (entry.Kind == "模型") { result = entry.Text; Finish("model_failed"); return; }
            }
            if (!Moving && waiting && result.EndsWith("→ reserve")) { Finish("reserved"); return; }

            // 实际位置偏移一厘米后才计为起步。等待动作沿用待机剪辑，两项分别测量。
            Vector3 pos = Technician.GetComponent<TransformComponent>().Translation;
            float dx = pos.X - origin.X, dz = pos.Z - origin.Z;
            if (Moving && result.EndsWith("→ share") && dx*dx + dz*dz >= .0001f) { Finish("moved"); return; }
            if (Milliseconds - started >= 10000) Finish("timeout");
        }
    }
}
