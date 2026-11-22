using System.Globalization;
using System.IO;
using Redflag;

namespace StationTest
{
    /* 在正式场景的运行副本中发令，并留下截帧所需的动作时刻。库存由原场景照常处理。 */
    public class MotionCapture : Entity
    {
        public Entity Technician, Player;
        private ulong cursor;
        private float time;
        private bool asked;

        void OnUpdate(float seconds)
        {
            time += seconds;
            if (!asked && time > 1)
            {
                asked = Npc.Submit(Technician.Id, "先捡起医疗包，再走到玩家身边把它交给玩家。");
                if (!asked) return;
            }
            NpcEvent entry;
            while (Npc.TryReadEvent(Technician.Id, ref cursor, out entry))
            {
                string row = string.Format(CultureInfo.InvariantCulture, "{0:F3}\t{1}\t{2}\t{3}\t{4}\t{5}\n",
                    time, entry.Kind, entry.Text, entry.Target, GameWorld.ItemCount(Technician.Id, "medkit"),
                    GameWorld.ItemCount(Player.Id, "medkit"));
                File.AppendAllText("station-motion.tsv", row);
            }
        }
    }
}
