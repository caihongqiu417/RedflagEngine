using System;
using Redflag;

namespace Sandbox
{
    /* 准星从镜头出发。臂炮的脉冲从角色右手出发，两条射线分别检查自己的遮挡。 */
    public class StationEquipment
    {
        public bool Cannon { get; private set; } = true;
        public ulong Focus { get; private set; }
        public float ShotTime { get; private set; }
        public float ChangeTime { get; private set; }
        private float cooldown;

        public string Update(Entity actor, TransformComponent camera, float seconds, bool canUse)
        {
            ChangeTime -= seconds;
            ShotTime -= seconds;
            cooldown -= seconds;
            bool slot = Cannon;
            if (canUse && Input.IsKeyDown(KeyCode.D1)) Cannon = true;
            if (canUse && Input.IsKeyDown(KeyCode.D2)) Cannon = false;
            if (slot != Cannon) ChangeTime = .35f;

            Vector3 eye = camera.Translation, r = camera.Rotation;
            float level = (float)Math.Cos(r.X);
            Vector3 forward = new Vector3(-(float)Math.Sin(r.Y)*level,(float)Math.Sin(r.X),-(float)Math.Cos(r.Y)*level);
            Vector3 aim;
            Focus = GameWorld.Ray(actor.Id,ref eye,ref forward,35,out aim);
            if (!canUse || ChangeTime > 0 || !Input.IsMouseButtonDown(MouseButton.Left) || cooldown > 0) return null;
            cooldown = .48f;

            if (!Cannon) {
                Playtest.Record(actor.Id,"scan",Focus.ToString());
                return Focus == 0 ? "扫描范围内没有物体" : "扫描到 " + GameWorld.Name(Focus);
            }
            Vector3 muzzle;
            // Godot 样例在右手节点正上方 0.4 米设了 ShootFrom，枪口随作者的动作一起移动。
            Vector3 socket = new Vector3(0,.4f,0);
            if (!GameWorld.Socket(actor.Id,"hand.R",ref socket,out muzzle)) return null;
            Vector3 direction = (aim-muzzle).Normalized(), impact;
            ulong hit = GameWorld.Ray(actor.Id,ref muzzle,ref direction,(aim-muzzle).Length()+.08f,out impact);
            ShotTime = .22f;
            GameWorld.Pulse(actor.Id,ref muzzle,ref impact);
            Playtest.Record(actor.Id,"pulse",hit.ToString());
            return hit != 0 && GameWorld.Damage(actor.Id,hit,25) ? "脉冲命中" : null;
        }
    }
}
