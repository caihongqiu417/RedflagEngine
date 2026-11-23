using System;
using Redflag;

namespace Sandbox
{
    /* 巡检机把看见的人和线圈状态写给行为树。巡逻、蓄能和搜索都从上帧继续。 */
    public class StationGuard : Entity
    {
        public Entity Player, Watt, PatrolEnd, WarningLamp;
        public float Sight = 6.4f, Reach = 2.2f, WalkSpeed = 1.9f, WorkRadius = 5f;
        public float ChargeTime = .8f, Cooldown = 1.2f, SearchTime = 2.5f, PulseDamage = 12f;

        private TransformComponent pose;
        private PointLightComponent light;
        private Vector3 home, lastSeen;
        private Entity target;
        private float charge, cooling, recoil, search, peakLight;
        private Vector3 lampOffset;
        private bool returning;
        private string action;
        private readonly string[] facts = new string[5];
        private Func<string, BehaviorStatus> tickTask;
        private Action<string> abortTask;
        private float frameSeconds;

        void OnCreate()
        {
            pose = GetComponent<TransformComponent>();
            tickTask = TickTask;
            abortTask = AbortTask;
            home = pose.Translation;
            if (WarningLamp != null) {
                light = WarningLamp.GetComponent<PointLightComponent>();
                peakLight = light.Intensity;
                lampOffset = WarningLamp.GetComponent<TransformComponent>().Translation - home;
                light.Intensity = 0;
            }
            // MASTER 是 Sentinel.glb 中带整台机器向前移动的骨骼。位移交给物理，剪辑保留迈步。
            GameWorld.SetMovementRoot(Id, "MASTER");
        }

        void OnUpdate(float seconds)
        {
            cooling = Math.Max(0f, cooling - seconds);
            recoil = Math.Max(0f, recoil - seconds);
            search = Math.Max(0f, search - seconds);
            Entity seen = null;
            if (GameWorld.Health(Id) > 0) {
                if (Visible(Player)) seen = Player;
                else if (Visible(Watt)) seen = Watt;
            }

            // 目标离开视野后只记得最后见到它的地方。换人或离开放电距离，都要重新蓄能。
            bool near = false;
            if (seen != null) {
                lastSeen = seen.GetComponent<TransformComponent>().Translation;
                search = SearchTime;
                near = (lastSeen - pose.Translation).Length() <= Reach;
            }
            if (seen != target) {
                Playtest.Record(Id, "sentinel_target", seen == null ? "lost" : seen.Id.ToString());
                charge = 0;
            }
            target = seen;
            facts[0] = GameWorld.Health(Id) <= 0 ? "damaged" : null;
            facts[1] = seen != null ? "visible" : null;
            facts[2] = !near ? "outside_reach" : null;
            facts[3] = cooling > 0 ? "cooling" : null;
            facts[4] = search > 0 ? "last_seen" : null;
            frameSeconds = seconds;
            GameWorld.TickBehavior(Id, facts, tickTask, abortTask);

            if (light != null) {
                WarningLamp.GetComponent<TransformComponent>().Translation = pose.Translation + lampOffset;
                // 缓慢点亮给玩家预警；脉冲那一帧使用美术在场景里设置的完整灯光强度。
                light.Intensity = action == "charge" ? peakLight * (.2f + .6f * Math.Min(1f, charge / ChargeTime)) :
                                  action == "discharge" ? peakLight : 0f;
            }
        }

        //========== 行为树的动作 ==========

        BehaviorStatus TickTask(string next)
        {
            if (next != action) {
                action = next;
                Playtest.Record(Id, "sentinel", action);
            }
            // 动作键来自行为树。下面的 Cannon-N、Walk-cycle 等名称保留 Sentinel.glb 的原名。
            switch (action) {
                case "damaged": GameWorld.Pose(Id, "Damage_heavy", .3f); break;
                case "chase": Walk(lastSeen, WalkSpeed, Reach * .94f, frameSeconds); break;
                case "charge":
                    Face(lastSeen);
                    GameWorld.PlayClip(Id, "Cannon-N");
                    charge += frameSeconds;
                    if (charge >= ChargeTime) return BehaviorStatus.Success;
                    break;
                case "discharge":
                    GameWorld.PlayClip(Id, "Cannon_Fire_Recoil");
                    recoil = GameWorld.ClipDuration(Id, "Cannon_Fire_Recoil");
                    Vector3 origin = pose.Translation + new Vector3(0,1.3f,0);
                    Vector3 end = lastSeen + new Vector3(0,.9f,0);
                    // 露出头部仍能被看到。放电时再沿实际画出的线检查，箱体会截住脉冲。
                    Vector3 beam = end - origin, impact;
                    ulong struck = GameWorld.Ray(Id, ref origin, ref beam, beam.Length() + .05f, out impact);
                    GameWorld.Pulse(Id, ref origin, ref impact);
                    if (struck == target.Id) GameWorld.Damage(Id, target.Id, PulseDamage);
                    Playtest.Record(Id, "sentinel_pulse", struck.ToString());
                    cooling = Cooldown;
                    charge = 0;
                    return BehaviorStatus.Success;
                case "cool":
                    Face(lastSeen);
                    // 放电后的回弹用模型中的原片时长，冷却动作会等它播完。
                    if (recoil <= 0) GameWorld.PlayClip(Id, "Idle-cycle");
                    break;
                case "search": Walk(lastSeen, WalkSpeed * .7f, .24f, frameSeconds); break;
                case "patrol":
                    if (PatrolEnd == null) { GameWorld.PlayClip(Id, "Idle-Eyes"); break; }
                    Vector3 mark = returning ? home : PatrolEnd.GetComponent<TransformComponent>().Translation;
                    if (Walk(mark, WalkSpeed * .55f, .16f, frameSeconds)) returning = !returning;
                    break;
                default: return BehaviorStatus.Failure;
            }
            return BehaviorStatus.Running;
        }

        void AbortTask(string interrupted)
        {
            if (interrupted == "charge") charge = 0;
            Playtest.Record(Id, "sentinel_abort", interrupted);
        }

        void OnDestroy()
        {
            GameWorld.HaltBehavior(Id, abortTask);
        }

        bool Visible(Entity other)
        {
            if (GameWorld.Health(other.Id) <= 0) return false;
            Vector3 point = other.GetComponent<TransformComponent>().Translation;
            if ((point - pose.Translation).Length() > Sight) return false;
            if (PatrolEnd != null) {
                // 作业范围沿两处巡查点展开。走出货场后，巡检机只去最后看见人的位置。
                Vector3 line = PatrolEnd.GetComponent<TransformComponent>().Translation - home;
                line.Y = 0;
                Vector3 delta = point - home;
                float t = line.LengthSquared() > 0 ? (delta.X * line.X + delta.Z * line.Z) / line.LengthSquared() : 0;
                Vector3 offset = delta - line * Math.Max(0f, Math.Min(1f, t));
                if (offset.X * offset.X + offset.Z * offset.Z > WorkRadius * WorkRadius) return false;
            }
            StationPlayer player = other.As<StationPlayer>();
            // 检测线从巡检机的观察窗出发，分别瞄准机器人站立或蹲下时的上身。高度单位为米。
            float height = player != null && player.Crouched ? 1f : 1.55f;
            Vector3 origin = pose.Translation + new Vector3(0,1.3f,0), hit;
            Vector3 ray = point + new Vector3(0,height,0) - origin;
            return GameWorld.Ray(Id,ref origin,ref ray,ray.Length() + .05f,out hit) == other.Id;
        }

        void Face(Vector3 point)
        {
            Vector3 delta = point - pose.Translation;
            pose.Rotation = new Vector3(0, (float)Math.Atan2(delta.X,delta.Z), 0);
        }

        bool Walk(Vector3 point, float speed, float stop, float seconds)
        {
            Vector3 delta = point - pose.Translation;
            delta.Y = 0;
            float distance = delta.Length();
            if (distance <= stop) { GameWorld.PlayClip(Id,"Idle-Eyes"); return true; }

            Face(point);
            // 最后一步稍微走进停步范围，避免世界坐标取整后永远差一小截。
            Vector3 step = delta * (Math.Min(speed * seconds, distance - stop + .01f) / distance);
            if (GameWorld.Move(Id,ref step,.34f)) GameWorld.PlayLocomotion(Id,"Walk-cycle");
            else GameWorld.PlayClip(Id,"Idle-Eyes");
            return false;
        }
    }
}
