using System.Runtime.CompilerServices;

namespace Redflag
{
    public struct NpcEvent
    {
        public double Time;
        public ulong Target;
        public string Kind, Text, Speech;
    }

    /* 指令交给当前游戏中的一个角色。脚本每帧可读取这个角色的回答和动作记录。 */
    public static class Npc
    {
        /// <summary>把玩家的话交给角色。闲聊时角色继续原来的工作，回答从事件的 Speech 读取。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Say(ulong actor, string text);

        /// <returns>角色有可执行的候选并开始请求模型时返回 true。动作的结果随后从 TryReadEvent 读取。</returns>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Submit(ulong actor, string instruction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Cancel(ulong actor);

        /// <param name="cursor">为这个角色保存上次读到的编号，从零开始。不同读者可以各自保存一份。</param>
        public static bool TryReadEvent(ulong actor, ref ulong cursor, out NpcEvent entry)
        {
            entry = new NpcEvent();
            double seconds;
            ulong target;
            string kind, text, speech;
            if (!ReadEvent(actor, ref cursor, out seconds, out target, out kind, out text, out speech))
                return false;

            entry.Time = seconds;
            entry.Target = target;
            entry.Kind = kind;
            entry.Text = text;
            entry.Speech = speech;
            return true;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool ReadEvent(ulong actor, ref ulong cursor, out double seconds,
            out ulong target, out string kind, out string text, out string speech);
    }
}
