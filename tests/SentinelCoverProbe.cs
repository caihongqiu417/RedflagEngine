using Redflag;

namespace StationTest
{
    /* 只移动测试隔板并记录时刻。巡检机本身仍由正式脚本、行为树和 PhysX 推进。 */
    public class SentinelCoverProbe : Entity
    {
        public Entity Guard, Player, Lamp;
        private int step;
        private float elapsed;

        void OnUpdate(float seconds)
        {
            float glow = Lamp.GetComponent<PointLightComponent>().Intensity;
            bool charging = glow > 8f && glow <= 21f;
            elapsed += seconds;
            if (step == 0 && glow >= 25f) Next("low_panel_pulse", -2);
            else if (step == 1 && charging) Next("hide_during_charge", 1.23f);
            else if (step == 2 && elapsed >= 3.8f) Next("reappear", -2);
            else if (step == 3 && GameWorld.Health(Player.Id) < 100) Next("open_pulse", -2);
            else if (step == 4 && charging) {
                GameWorld.Damage(0,Guard.Id,GameWorld.Health(Guard.Id));
                Next("break_during_charge", -2);
            } else if (step == 5 && elapsed > 1.25f) Next("finished", -2);
        }

        void Next(string name, float height)
        {
            TransformComponent pose = GetComponent<TransformComponent>();
            Vector3 p = pose.Translation;
            p.Y = height;
            pose.Translation = p;
            Playtest.Record(Id, "cover_probe", name);
            ++step;
            elapsed = 0;
        }
    }
}
