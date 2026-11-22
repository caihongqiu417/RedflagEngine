using System;
using Redflag;

namespace Workshop
{
    public class RepairOrder : Entity
    {
        private ulong cursor;

        void OnCreate()
        {
            if (!Npc.Submit(Id, "修好身边的电梯。"))
                Console.WriteLine("维修员目前没有可以处理的设备。");
        }

        void OnUpdate(float seconds)
        {
            NpcEvent entry;
            while (Npc.TryReadEvent(Id, ref cursor, out entry))
                Console.WriteLine("{0} | {1} | {2} | {3}", entry.Kind, entry.Text, entry.Target, entry.Speech);
        }

        void OnDestroy()
        {
            // 在游戏中移除这个脚本时，一并撤销它交给维修员的工作。
            Npc.Cancel(Id);
        }
    }
}
