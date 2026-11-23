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
        public bool Landing => landing;
        private Vector3 velocity;
        private float verticalSpeed;
        private bool landing;
        private TraversalMode traversal;
        private bool jumpWasDown;

        internal void Update(Entity actor, TransformComponent pose, float yaw, bool aiming, float walkSpeed,
            float seconds, bool busy, OperatorCollision body, float crouchHeight)
        {
            Crouched = !busy && Input.IsKeyDown(KeyCode.LeftControl) && !Airborne;
            body.Crouch(Crouched, crouchHeight);
            float sin = (float)Math.Sin(yaw), cos = (float)Math.Cos(yaw);
            Vector3 forward = new Vector3(-sin,0,-cos), right = new Vector3(cos,0,-sin);
            Vector3 direction = (forward*Axis(KeyCode.W,KeyCode.S)+right*Axis(KeyCode.D,KeyCode.A)).Normalized();
            Running = !Crouched && Input.IsKeyDown(KeyCode.LeftShift) && direction.LengthSquared() > 0;
            float speed = Crouched ? 1.1f : Running ? 5.4f : walkSpeed;
            velocity += (direction*speed-velocity)*(1f-(float)Math.Exp(-9f*seconds));
            if (busy) velocity = Vector3.Zero;
            bool jump = Input.IsKeyDown(KeyCode.Space);
            TraversalInput request = jump && !jumpWasDown && !Crouched && !busy ? TraversalInput.Jump : TraversalInput.Walk;
            jumpWasDown = jump;

            Vector3 start = pose.Translation;
            bool wasAirborne = Airborne;
            Vector3 delta = velocity * seconds;
            traversal = GameWorld.TraverseWithCollider(actor.Id, ref delta, seconds, request);
            Airborne = traversal == TraversalMode.Jump || traversal == TraversalMode.Climb;
            Vector3 moved = pose.Translation - start;
            verticalSpeed = seconds > 0 ? moved.Y / seconds : 0;
            moved.Y = 0;
            // 按住前进贴墙时，碰撞可能拦下整步。角色按实际走出的距离选择动作。
            Walking = moved.Length() > .05f*seconds;
            Running = Running && Walking;
            if (wasAirborne && !Airborne) landing=Animator.Play(actor.Id,"land",true);
            else if (landing && !Animator.IsPlaying(actor.Id)) landing=false;

            if (!busy && (Walking || aiming)) {
                Vector3 facing = aiming ? forward : moved;
                float rotation = pose.Rotation.Y;
                float angle = (float)Math.Atan2(facing.X,facing.Z)-rotation;
                float turn = (float)Math.Atan2(Math.Sin(angle),Math.Cos(angle));
                pose.Rotation = new Vector3(0,rotation+turn*(1f-(float)Math.Exp(-12f*seconds)),0);
            }
        }

        public void UpdateDown(ulong id, float seconds)
        {
            // 脚离开地面之后仍须逐帧走完这次下落。停止接受移动输入，保留原来的落点。
            velocity = Vector3.Zero;
            Crouched = Walking = Running = landing = Recovering = false;
            Vector3 delta = Vector3.Zero;
            traversal = GameWorld.TraverseWithCollider(id, ref delta, seconds, TraversalInput.Walk);
            Airborne = traversal == TraversalMode.Jump || traversal == TraversalMode.Climb;
        }

        public void OnNotice(AnimationEvent notice)
        {
            if (notice.State=="land" && notice.Name=="Recovery") Recovering=notice.Phase==NotifyPhase.Begin;
        }

        public void Animate(ulong id, StationEquipment equipment)
        {
            string clip;
            if (traversal == TraversalMode.Climb) clip="Station-Pickup";
            else if (Airborne) clip=verticalSpeed>2.2f ? "jump_1_up" : "jump_3_midair-cycle";
            else if (landing) return; // 已由动作表播放落地片段，等剪辑结束再切回行走。
            else if (!Crouched && equipment.ChangeTime>0) clip="Cannon_activation-noexp";
            else if (!Crouched && equipment.ShotTime>0) clip="Cannon_Charge";
            else if (Walking) {
                if (Crouched) clip="Station-CrouchWalk";
                else if (Running) clip=equipment.Cannon ? "running_gun-cycle" : "running_nogun-cycle";
                else clip=equipment.Cannon ? "walking_gun-cycle" : "walking_nogun-cycle";
                GameWorld.PlayLocomotion(id,clip);
                return;
            }
            else if (Crouched) clip="Station-Crouch";
            else clip=Input.IsMouseButtonDown(MouseButton.Right) && equipment.Cannon ? "AIM-Center" : "Idle-cycle";
            GameWorld.PlayClip(id,clip);
        }

        static int Axis(KeyCode positive, KeyCode negative)
        {
            return (Input.IsKeyDown(positive)?1:0)-(Input.IsKeyDown(negative)?1:0);
        }
    }
}
