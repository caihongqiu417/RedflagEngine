using System.Runtime.CompilerServices;

namespace Redflag
{
    /// <summary>把脚本中的文字和进度交给这个实体的 HUD。名字与布局里的 Text 或 Meter 对应。</summary>
    public sealed class Hud
    {
        private readonly ulong owner;

        public Hud(ulong owner) { this.owner = owner; }

        public void SetText(string name, string value)
        {
            WriteText(owner, name, value ?? "");
        }

        /// <param name="fraction">填满的比例。0 表示空，1 表示满。</param>
        public void SetMeter(string name, float fraction)
        {
            WriteMeter(owner, name, fraction);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void WriteText(ulong owner, string name, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void WriteMeter(ulong owner, string name, float fraction);
    }
}
