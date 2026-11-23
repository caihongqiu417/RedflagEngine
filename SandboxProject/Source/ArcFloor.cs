using Redflag;

namespace Sandbox
{
    /* 断电时车道电缆间歇放电。修好发电机后，车道恢复通行，出口灯亮起。 */
    public class ArcFloor : Entity
    {
        public Entity Player, Watt, Signal, Relay, ExitLight;
        // 在 Forge 的脚本属性中调整周期、通电时长（秒）及碰撞半径（米）。
        public float SignalIntensity = 8f, Cycle = 4f, ActiveSeconds = 1f, StartPhase = 2.5f;
        public float Radius = 2.1f, JumpClearance = .45f, Damage = 20f;
        private float elapsed;
        private bool active, powered;

        void OnCreate() { elapsed = StartPhase; }

        void OnUpdate(float seconds)
        {
            if (powered) return;
            if (Relay != null && GameWorld.Health(Relay.Id) >= GameWorld.MaxHealth(Relay.Id)) {
                powered = true;
                Signal.GetComponent<PointLightComponent>().Intensity = 0;
                if (ExitLight != null) ExitLight.GetComponent<PointLightComponent>().Intensity = SignalIntensity;
                Playtest.Record(Id, "lane_clear", "供电恢复，电缆停止放电");
                return;
            }
            elapsed = (elapsed + seconds) % Cycle;
            bool on = elapsed < ActiveSeconds;
            if (on == active) return;
            active = on;
            // 电网通电时点亮警示灯。灯架一直立在原处。
            Signal.GetComponent<PointLightComponent>().Intensity = on ? SignalIntensity : 0f;
            if (on) {
                Shock(Player);
                Shock(Watt);
            }
        }

        void Shock(Entity target)
        {
            float feet = target.GetComponent<TransformComponent>().Translation.Y;
            if (feet < GetComponent<TransformComponent>().Translation.Y + JumpClearance)
                GameWorld.Hit(Id,target.Id,Radius,Damage);
        }
    }
}
