using System.Runtime.CompilerServices;

namespace Redflag
{
    public enum NotifyPhase { Point, Begin, End }

    public struct AnimationEvent
    {
        public string State, Name;
        public NotifyPhase Phase;
        public float Seconds;
        public bool Interrupted;
    }

    public static class Animator
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Bind(ulong actor, string file);

        /// <summary>从该状态的开头播放。同一动作每帧重复请求会继续播放，restart=true 才重新开始。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Play(ulong actor, string state, bool restart = false);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool IsOpen(ulong actor, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool IsPlaying(ulong actor);

        /// <summary>取走一条通知。在 OnUpdate 中用 while 读完，单帧同时越过窗口的起止时刻也都能收到。</summary>
        public static bool TryRead(ulong actor, out AnimationEvent notice)
        {
            notice = new AnimationEvent();
            if (!Read(actor, out string state, out string name, out NotifyPhase phase, out float seconds, out bool interrupted))
                return false;

            notice = new AnimationEvent { State=state, Name=name, Phase=phase, Seconds=seconds, Interrupted=interrupted };
            return true;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool Read(ulong actor,out string state,out string name,out NotifyPhase phase,out float seconds,out bool interrupted);
    }
}
