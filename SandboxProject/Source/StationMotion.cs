using System;
using Redflag;

namespace Sandbox
{
    public class StationMotion
    {
        public bool Crouched { get; private set; }
        public bool Airborne { get; private set; }
        public bool Running { get; private set; }
        public bool Walking { get; private set; }
        public bool Recovering { get; private set; }
        private Vector3 velocity;
        private float verticalSpeed;
        private bool landing;
        private TraversalMode traversal;
        private bool jumpWasDown;

        public void Update(Entity actor, TransformComponent pose, float yaw, bool aiming, float walkSpeed, float seconds)
        {
            bool crouch = Input.IsKeyDown(KeyCode.LeftControl) && !Airborne;
            if (crouch != Crouched) {
                Crouched = crouch;
                GameWorld.BodyHeight(actor.Id,crouch ? 1.40f : 1.84f);
            }
            float sin = (float)Math.Sin(yaw), cos = (float)Math.Cos(yaw);
            Vector3 forward = new Vector3(-sin,0,-cos), right = new Vector3(cos,0,-sin);
            Vector3 direction = (forward*Axis(KeyCode.W,KeyCode.S)+right*Axis(KeyCode.D,KeyCode.A)).Normalized();
            Running = !Crouched && Input.IsKeyDown(KeyCode.LeftShift) && direction.LengthSquared() > 0;
            float speed = Crouched ? 1.1f : Running ? 5.4f : walkSpeed;
            velocity += (direction*speed-velocity)*(1f-(float)Math.Exp(-9f*seconds));
            Walking = velocity.LengthSquared()>.012f;
            bool jump = Input.IsKeyDown(KeyCode.Space);
            TraversalInput request = jump && !jumpWasDown && !Crouched ? TraversalInput.Jump : TraversalInput.Walk;
            jumpWasDown = jump;
            float height = pose.Translation.Y;
            bool wasAirborne = Airborne;
            Vector3 delta = velocity * seconds;
            traversal = GameWorld.Traverse(actor.Id,ref delta,.34f,seconds,request);
            Airborne = traversal == TraversalMode.Jump || traversal == TraversalMode.Climb;
            verticalSpeed = seconds > 0 ? (pose.Translation.Y - height) / seconds : 0;
            if (wasAirborne && !Airborne) landing=Animator.Play(actor.Id,"land",true);
            else if (landing && !Animator.IsPlaying(actor.Id)) landing=false;

            AnimationEvent notice;
            while (Animator.TryRead(actor.Id,out notice))
                if (notice.State=="land" && notice.Name=="Recovery") Recovering=notice.Phase==NotifyPhase.Begin;

            if (Walking || aiming) {
                Vector3 facing = aiming ? forward : velocity;
                float rotation = pose.Rotation.Y;
                float angle = (float)Math.Atan2(facing.X,facing.Z)-rotation;
                float turn = (float)Math.Atan2(Math.Sin(angle),Math.Cos(angle));
                pose.Rotation = new Vector3(0,rotation+turn*(1f-(float)Math.Exp(-12f*seconds)),0);
            }
        }

        public void Animate(ulong id, StationEquipment equipment)
        {
            if (Crouched) {
                // 落地片段的第 2 帧（24 帧/秒）屈膝最深，站点用它作掩体后的蹲姿。
                GameWorld.Pose(id,"jump_5_hardlanding",2f/24f);
                return;
            }
            string clip;
            if (traversal == TraversalMode.Climb) clip="Station-Pickup";
            else if (Airborne) clip=verticalSpeed>2.2f ? "jump_1_up" : "jump_3_midair-cycle";
            else if (landing) return; // 已由动作表播放落地片段，等剪辑结束再切回行走。
            else if (equipment.ChangeTime>0) clip="Cannon_activation-noexp";
            else if (equipment.ShotTime>0) clip="Cannon_Charge";
            else if (Running) clip=equipment.Cannon ? "running_gun-cycle" : "running_nogun-cycle";
            else if (Walking) clip=equipment.Cannon ? "walking_gun-cycle" : "walking_nogun-cycle";
            else clip=Input.IsMouseButtonDown(MouseButton.Right) && equipment.Cannon ? "AIM-Center" : "Idle-cycle";
            GameWorld.PlayClip(id,clip);
        }

        static int Axis(KeyCode positive, KeyCode negative)
        {
            return (Input.IsKeyDown(positive)?1:0)-(Input.IsKeyDown(negative)?1:0);
        }
    }
}
