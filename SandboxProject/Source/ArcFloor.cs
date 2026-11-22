using Redflag;

namespace Sandbox
{
    /* 电网开启时，地面上的蓝条升起。每次通电分别检查玩家和瓦特。 */
    public class ArcFloor : Entity
    {
        public Entity Player, Watt, Signal;
        public float SignalIntensity = 8f;
        private float elapsed = 2.5f;
        private bool active;

        void OnUpdate(float seconds)
        {
            elapsed = (elapsed + seconds) % 4f;
            bool on = elapsed < 1f;
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
            if (feet < GetComponent<TransformComponent>().Translation.Y + .45f)
                GameWorld.Hit(Id,target.Id,2.1f,20f);
        }
    }
}
