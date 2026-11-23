using System.Runtime.CompilerServices;

namespace Redflag
{
    public struct MontageEvent
    {
        public string Slot, Section, State, Name;
        public NotifyPhase Phase;
        public float Seconds;
        public bool Interrupted;
    }

    /// <summary>把一个动作叠加到角色已有的走跑姿势上。动作文件指定骨骼、播放段和要通知游戏的时刻。</summary>
    public static class Montage
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Play(ulong actor, string file, string section = "", bool restart = false);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Jump(ulong actor, string slot, string section);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Stop(ulong actor, string slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool IsPlaying(ulong actor, string slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool IsOpen(ulong actor, string slot, string window);

        /// <summary>在同名窗口内沿地面移向世界坐标 position，最终朝向以弧度表示。角色仍会被碰撞体挡住。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool SetWarpTarget(ulong actor, string name, ref Vector3 position, float yawRadians);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool ClearWarpTarget(ulong actor, string name);

        public static bool TryRead(ulong actor, out MontageEvent notice)
        {
            notice = new MontageEvent();
            return Read(actor, out notice.Slot, out notice.Section, out notice.State, out notice.Name,
                out notice.Phase, out notice.Seconds, out notice.Interrupted);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool Read(ulong actor, out string slot, out string section, out string state, out string name,
            out NotifyPhase phase, out float seconds, out bool interrupted);
    }
}
