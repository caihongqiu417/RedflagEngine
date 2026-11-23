using System;
using Redflag;

namespace Sandbox
{
    /* F 播放一次左拳。动作通知到达拳峰时查询碰撞，收拳后交还移动控制。 */
    public sealed class StationMelee
    {
        public bool Active { get; private set; }
        private bool held;

        public bool ReadPress()
        {
            bool down = Input.IsKeyDown(KeyCode.F);
            bool pressed = down && !held;
            held = down;
            return pressed;
        }

        public void Begin(ulong actor, TransformComponent pose, float cameraYaw)
        {
            if (!Animator.Play(actor, "punch", true)) return;
            pose.Rotation = new Vector3(0, cameraYaw + (float)Math.PI, 0);
            Active = true;
            Playtest.Record(actor, "punch_begin", "左拳起手");
        }

        public string Contact(ulong actor, ulong opponent, TransformComponent pose)
        {
            // 作者剪辑的第 6 帧：左腕在模型中心左侧 0.145 米、高 1.249 米，指节向前伸至 0.896 米。
            // 用这帧的触点判断。帧率较低时，收到通知时屏幕上可能已经开始收拳。
            float yaw = pose.Rotation.Y;
            Vector3 forward = new Vector3((float)Math.Sin(yaw), 0, (float)Math.Cos(yaw));
            Vector3 side = new Vector3(forward.Z, 0, -forward.X);
            Vector3 origin = pose.Translation + side*.145f + new Vector3(0,1.249f,0);
            Vector3 point;
            ulong first = GameWorld.Ray(actor, ref origin, ref forward, .896f, out point);
            bool hit = first == opponent && GameWorld.Damage(actor, first, 15);
            Playtest.Record(actor, hit ? "punch_hit" : "punch_miss", first.ToString());
            return hit ? "近战命中" : "挥空";
        }

        public void End(ulong actor, bool interrupted)
        {
            Active = false;
            Playtest.Record(actor, interrupted ? "punch_cancelled" : "punch_end", interrupted ? "玩家倒地" : "收拳");
        }
    }
}
