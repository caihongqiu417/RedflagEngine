using System;
using Redflag;

namespace Sandbox
{
    /* 故障巡检机。靠近它时会被跟踪，离开它的工作区就能脱身。 */
    public class StationGuard : Entity
    {
        public Entity Player, Watt;
        private float cooldown;
        private TransformComponent pose;

        void OnCreate() { pose = GetComponent<TransformComponent>(); }

        void OnUpdate(float seconds)
        {
            if (GameWorld.Health(Id) <= 0) { GameWorld.Pose(Id, "Damage_heavy", .3f); return; }
            cooldown -= seconds;
            Entity target = GameWorld.Health(Player.Id) > 0 ? Player : Watt;
            Vector3 delta = target.GetComponent<TransformComponent>().Translation - pose.Translation;
            delta.Y = 0;
            float distance = delta.Length();
            StationPlayer player = target.As<StationPlayer>();
            bool low = player != null && player.Crouched;
            Vector3 origin=pose.Translation+new Vector3(0,1.3f,0);
            Vector3 sight=target.GetComponent<TransformComponent>().Translation+new Vector3(0,low ? 1f : 1.72f,0)-origin, hit;
            ulong seen=GameWorld.Ray(Id,ref origin,ref sight,sight.Length()+.05f,out hit);
            if (distance>6 || GameWorld.Health(target.Id)<=0 || seen!=target.Id) {
                GameWorld.PlayClip(Id,"Idle-cycle");
                return;
            }

            if (distance > 2.1f) {
                Vector3 direction = delta / distance;
                Vector3 step = direction * Math.Min(1.7f * seconds, distance - 2.1f);
                GameWorld.Move(Id, ref step, .34f);
                pose.Rotation = new Vector3(0, (float)Math.Atan2(direction.X,direction.Z), 0);
                GameWorld.PlayClip(Id, "Walk-cycle");
            } else {
                GameWorld.PlayClip(Id, "Cannon_Fire_Recoil");
                // 两次放电至少间隔一秒，持续接触时也不会每帧扣血。
                if (cooldown <= 0 && GameWorld.Damage(Id, target.Id, 12f)) cooldown = 1f;
            }
        }
    }
}
