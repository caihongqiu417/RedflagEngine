using System.Runtime.CompilerServices;

namespace Redflag
{
    public enum InteractionResult { Running, Completed, Cancelled, MissingTarget, OutOfReach, Unavailable }
    public enum TraversalMode { Ground, Slope, Step, Jump, Climb, Blocked }
    public enum TraversalInput { Walk, Jump, Automatic }
    public enum TraversalLimb { LeftFoot, RightFoot, LeftHand, RightHand }

    public static class GameWorld
    {
        /// <summary>将当前成立的条件交给角色的行为树，返回第一个符合条件的动作名。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern string ChooseBehavior(ulong actor, string[] conditions);

        // ---- 移动与动画 ----

        /// <summary>沿地面移动，墙和箱子会挡住角色。</summary>
        /// <param name="delta">这一帧要走的世界坐标位移，单位为米。</param>
        /// <param name="radius">角色中心到身体边缘的距离，单位为米。</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Move(ulong actor, ref Vector3 delta, float radius);

        /// <summary>按地形移动一帧。按下跳跃时传 Jump，队友可以传 Automatic 自行寻找落脚处。</summary>
        /// <param name="delta">这一帧的水平位移，单位为米。停下时也继续传零，让跳跃播放到落地。</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern TraversalMode Traverse(ulong actor, ref Vector3 delta, float radius, float seconds, TraversalInput input);

        /// <summary>按角色碰撞体的实际半径检查墙面和平台边缘。在 Forge 调宽胶囊后，角色移动时也会给身体留出更大的空间。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern TraversalMode TraverseWithCollider(ulong actor, ref Vector3 delta, float seconds, TraversalInput input);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void CancelTraversal(ulong actor);

        /// <summary>从模型中选三根骨头。例如指定大腿、小腿和脚踝后，脚会随各自踩到的坡面调整。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void SetTraversalLimb(ulong actor, TraversalLimb limb, string upper, string lower, string tip);

        /// <summary>设置腿骨后，在鞋跟、前掌等位置登记脚底点。坐标相对指定的骨头，随它一起运动。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void AddFootContact(ulong actor, TraversalLimb foot, string bone, ref Vector3 point);

        /// <summary>指出动画中带整个人移动的根骨。用物理移动角色时，由引擎保留这根骨的初始位置。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void SetMovementRoot(ulong actor, string node);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void PlayClip(ulong actor, string clip);

        /// <summary>按角色在地面上实际移动的距离播放步态。使用 SetMovementRoot 指定剪辑中记录步幅的骨骼。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void PlayLocomotion(ulong actor, string clip);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Pose(ulong actor, string clip, float seconds);

        // ---- 生命、物品与交互 ----

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern float Health(ulong actor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern float MaxHealth(ulong actor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern int ItemCount(ulong actor, string item);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Heal(ulong actor, string item, float amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Hit(ulong actor, ulong target, float range, float amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool ApproachContact(ulong actor, ulong target, string action, float seconds);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool BeginContact(ulong actor, ulong target, string action);

        /// <summary>比较画面中手的握持点与目标。递物时，两个人的手都要够到交接位置。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool AtContact(ulong actor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void EndContact(ulong actor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool CanCollect(ulong actor, ulong target, float range);

        /// <summary>手碰到物品时再次查距离和库存，成功后把物品交给角色。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Collect(ulong actor, ulong target, float range);

        /// <summary>双方仍够得到对方且库存足够时，把指定数量的物品从交付者转入接收者的背包。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Give(ulong actor, ulong recipient, string item, int count, float range);

        /// <summary>按下交互键时传 start=true 开始计时。按住时传本帧秒数，松手后下次从头开始。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern InteractionResult Interact(ulong actor, ulong target, string action, float seconds, bool start);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Hud(ulong actor, string title, string objective, string status, string controls, bool finished);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Label(ulong actor, ref Vector3 color, float height);

        /// <summary>从起点沿方向推动一个球，返回碰到场景前能走的米数。跳过属于 actor 的碰撞体。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern float SweepSphere(ulong actor, ref Vector3 origin, ref Vector3 direction, float radius, float distance);

        /// <summary>返回射线先碰到的实体编号与世界坐标。没有碰撞时返回 0，point 是射线的终点。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern ulong Ray(ulong actor, ref Vector3 origin, ref Vector3 direction, float distance, out Vector3 point);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Damage(ulong actor, ulong target, float amount);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern string Name(ulong id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void Pulse(ulong actor, ref Vector3 from, ref Vector3 to);

        /// <summary>将骨骼局部坐标的 offset 换成当前姿势里的世界位置。没有这个节点时返回 false。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Socket(ulong actor, string bone, ref Vector3 offset, out Vector3 point);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void BodyHeight(ulong actor, float height);
    }
}
