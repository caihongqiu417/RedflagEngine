using System;
using Redflag;

namespace Sandbox
{
    /* 镜头跟随主角肩部。按住鼠标右键绕主角观察，贴近设备舱时镜头会前移。 */
    public class ThirdPersonCamera : Entity
    {
        public Entity Target;
        public float Distance = 4.7f, Height = 1.38f;
        public float Shoulder = .62f;
        // 米。箱子挡住肩旁视线时，镜头最多抬高这么多；设为 0 就只缩近。
        public float ObstacleLift = .6f;
        private TransformComponent pose, actor;
        private Vector3 focus;
        private float yaw, pitch, desiredYaw, desiredPitch, arm, lift, lastX, lastY;
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

            // 跟随的是双肩中间的位置，偏向右肩的这段距离也要经过碰撞检查。
            float follow = 1f - (float)Math.Exp(-12f*seconds);
            StationPlayer player = Target.As<StationPlayer>();
            float eye = player != null && player.Crouched ? Height-.34f : Height;
            yaw += (desiredYaw-yaw)*follow;
            pitch += (desiredPitch-pitch)*follow;
            focus += (actor.Translation + new Vector3(0,eye,0) - focus)*follow;
            Position(seconds);
        }

        void OnDestroy()
        {
            if (dragging) Input.CaptureCursor(false);
        }

        const float CameraRadius = .20f;

        float Clearance(Vector3 from, Vector3 direction, float length)
        {
            float free = GameWorld.SweepSphere(Target.Id, ref from, ref direction, CameraRadius, length);
            // 碰到表面时留出一厘米。下一次从这里向后探测，起点仍在墙外。
            return free < length ? Math.Max(0f, free-.01f) : length;
        }

        Vector3 ShoulderPosition(Vector3 right, float rise)
        {
            Vector3 offset = right*Shoulder + new Vector3(0,rise,0);
            float length = offset.Length();
            if (length == 0f) return focus;
            Vector3 direction = offset*(1f/length);
            return focus + direction*Clearance(focus,direction,length);
        }

        void Position(float seconds)
        {
            float level = (float)Math.Cos(pitch);
            Vector3 back = new Vector3((float)Math.Sin(yaw)*level, -(float)Math.Sin(pitch), (float)Math.Cos(yaw)*level);
            Vector3 right = new Vector3((float)Math.Cos(yaw),0,-(float)Math.Sin(yaw));
            Vector3 pivot = ShoulderPosition(right,0);
            float free = Clearance(pivot,back,Distance);

            // 只剩一半跟随距离时试一下箱顶。若头顶有横梁或面前是高墙，继续使用原来的位置。
            float desiredLift = 0;
            if (ObstacleLift > 0 && free < Distance*.5f) {
                Vector3 raised = ShoulderPosition(right,ObstacleLift);
                if (Clearance(raised,back,Distance) > free+CameraRadius) desiredLift = ObstacleLift;
            }
            lift += (desiredLift-lift)*(1f-(float)Math.Exp(-12f*seconds));
            if (lift < .001f) lift = 0f;
            if (lift > 0f) {
                pivot = ShoulderPosition(right,lift);
                free = Clearance(pivot,back,Distance);
            }
            arm = Math.Min(free, arm + (Distance-arm)*(1f-(float)Math.Exp(-6f*seconds)));
            pose.Translation = pivot + back*arm;
            pose.Rotation = new Vector3(pitch, yaw, 0);
        }
    }
}
