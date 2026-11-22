using System.Runtime.CompilerServices;

namespace Redflag
{
    public enum EvidenceSource { Seen, LastSeen, Reported, Photo }

    public struct NpcEvidence
    {
        public EvidenceSource Source;
        public ulong Target;
        public string Text, Image;
        public double Seconds;
    }

    public static class NpcNotes
    {
        /// <summary>将玩家的原话记在该角色名下。target 可填实体编号，只有图片时填 0 并给出图片编号。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool Report(ulong actor, ulong target, string text, string image = "");

        /// <summary>从 0 开始读取最近一次观察留下的记录。记录时间为本局开始以来的秒数。</summary>
        public static bool TryRead(ulong actor, int index, out NpcEvidence entry)
        {
            entry = new NpcEvidence();
            return Read(actor, index, out entry.Source, out entry.Target, out entry.Text, out entry.Image, out entry.Seconds);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool Read(ulong actor, int index, out EvidenceSource source, out ulong target,
            out string text, out string image, out double seconds);

        /// <summary>只接受角色配置允许的偏好名，传入空值可清除这一项。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool SetPreference(ulong actor, string key, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern string GetPreference(ulong actor, string key);

        /// <summary>返回可放进玩家存档的文字。只包含作者标为 saved 的偏好。</summary>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern string SavePreferences(ulong actor);

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern bool LoadPreferences(ulong actor, string save);
    }
}
