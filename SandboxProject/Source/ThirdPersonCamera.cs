using System;
using Redflag;

namespace Sandbox
{
    /* 镜头跟随主角肩部。按住鼠标右键绕主角观察，贴近设备舱时镜头会前移。 */
    public class ThirdPersonCamera : Entity
    {
        public Entity Target;
        public float Distance = 4.7f, Height = 1.38f;
        private TransformComponent pose, actor;
        private Vector3 focus;
        private float yaw, pitch, desiredYaw, desiredPitch, arm, lastX, lastY;
        private bool dragging;

        void OnCreate()
        {
            pose = GetComponent<TransformComponent>();
            actor = Target.GetComponent<TransformComponent>();
            yaw = desiredYaw = pose.Rotation.Y;
            pitch = desiredPitch = pose.Rotation.X;
            arm = Distance;
            focus = actor.Translation + new Vector3(0, Height, 0);
            Position(0);
        }

        void OnUpdate(float seconds)
        {
            float x, y;
            bool orbit = Input.IsMouseButtonDown(MouseButton.Right);
            if (orbit != dragging) Input.CaptureCursor(orbit);
            Input.MousePosition(out x,out y);
            if (orbit && dragging) {
                const float radiansPerPixel = .0045f;
                desiredYaw -= (x-lastX)*radiansPerPixel;
                desiredPitch = Math.Max(-.88f, Math.Min(.2f, desiredPitch-(y-lastY)*radiansPerPixel));
            }
            lastX = x; lastY = y; dragging = orbit;

            // 先跟随肩部，再缩短撞到墙的镜头距离。走出墙角时镜头缓慢退回。
            float follow = 1f - (float)Math.Exp(-12f*seconds);
            StationPlayer player = Target.As<StationPlayer>();
            float eye = player != null && player.Crouched ? Height-.34f : Height;
            Vector3 right = new Vector3((float)Math.Cos(yaw),0,-(float)Math.Sin(yaw));
            focus += (actor.Translation + new Vector3(0,eye,0) + right*.62f - focus)*follow;
            yaw += (desiredYaw-yaw)*follow;
            pitch += (desiredPitch-pitch)*follow;
            Position(seconds);
        }

        void OnDestroy()
        {
            if (dragging) Input.CaptureCursor(false);
        }

        void Position(float seconds)
        {
            float level = (float)Math.Cos(pitch);
            Vector3 back = new Vector3((float)Math.Sin(yaw)*level, -(float)Math.Sin(pitch), (float)Math.Cos(yaw)*level);
            float free = GameWorld.SweepSphere(Target.Id, ref focus, ref back, .20f, Distance);
            arm = Math.Min(free, arm + (Distance-arm)*(1f-(float)Math.Exp(-6f*seconds)));
            pose.Translation = focus + back*arm;
            pose.Rotation = new Vector3(pitch, yaw, 0);
        }
    }
}
