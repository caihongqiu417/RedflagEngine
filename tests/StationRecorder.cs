using System.Globalization;
using System.IO;
using Redflag;

namespace StationTest
{
    /* 只观察正式关卡。输入由 Windows 测试发送，移动、扣血和维修仍由游戏脚本完成。 */
    public class Recorder : Entity
    {
        public Entity Player, Guard, Relay, Camera;
        public bool RecordLanding;
        public bool InterruptPunch;
        private float time, lastTime = -1;
        private bool lastRecovery;

        void OnUpdate(float seconds)
        {
            time += seconds;
            Sandbox.StationPlayer player = Player.As<Sandbox.StationPlayer>();
            if (InterruptPunch && player.Punching) {
                InterruptPunch = false;
                GameWorld.Damage(Guard.Id, Player.Id, 100);
            }
            if (RecordLanding && player.LandingRecovery != lastRecovery)
            {
                lastRecovery = player.LandingRecovery;
                File.AppendAllText("landing.csv", string.Format(CultureInfo.InvariantCulture,
                    "{0:F6},{1},{2:F6}\n",time,lastRecovery ? "Begin" : "End",seconds));
            }
            if (time-lastTime < .06f) return;
            lastTime = time;
            Vector3 pose = Player.GetComponent<TransformComponent>().Translation;
            Vector3 camera = Camera.GetComponent<TransformComponent>().Translation;
            Vector3 look = Camera.GetComponent<TransformComponent>().Rotation;
            Vector3 guard = Guard.GetComponent<TransformComponent>().Translation;
            string line = string.Format(CultureInfo.InvariantCulture,
                "{0:F3},{1:F3},{2:F3},{3},{4},{5},{6},{7:F3},{8:F3},{9:F3},{10:F3},{11:F4},{12:F4},{13},{14},{15},{16},{17},{18:F3},{19:F3}\n",
                time,pose.X,pose.Z,GameWorld.Health(Player.Id),GameWorld.Health(Guard.Id),GameWorld.Health(Relay.Id),
                GameWorld.ItemCount(Player.Id,"medkit"),pose.Y,camera.X,camera.Y,camera.Z,look.Y,look.X,player.Focus,
                player.Crouched?1:0,player.Airborne?1:0,player.Running?1:0,player.Cannon?1:0,guard.X,guard.Z);
            File.AppendAllText("station.csv",line);
        }
    }

    // 旁边两份模型共用主角的动作表。一位播完落地，另一位在窗口开启后立即切换动作。
    public class NotifyActor : Entity
    {
        public bool Interrupt;

        void OnCreate()
        {
            if (!Animator.Bind(Id, "assets/ai/watt.animator.yaml") || !Animator.Play(Id, "land"))
                throw new System.InvalidOperationException("Cannot play the landing clip");
            if (Interrupt) Animator.Play(Id, "idle");
        }

        void OnUpdate(float seconds)
        {
            AnimationEvent notice;
            while (Animator.TryRead(Id, out notice))
                File.AppendAllText("notices.csv", string.Format(CultureInfo.InvariantCulture,
                    "{0},{1},{2},{3},{4:R},{5}\n", Id, notice.State, notice.Name, notice.Phase, notice.Seconds, notice.Interrupted));
        }
    }
}
