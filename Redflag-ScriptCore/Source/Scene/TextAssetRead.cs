using System.Runtime.CompilerServices;

namespace Redflag
{
    /* yield return 这次读取时，工作线程从项目的 assets 目录读 UTF-8 文件。
       脚本恢复后在游戏线程取 Text；失败时 Error 给出文件名和原因。 */
    public sealed class TextAssetRead : GameWait
    {
        private readonly string path;
        private ulong request;

        public TextAssetRead(string assetPath) { path = assetPath; }
        public string Text { get; private set; }
        public string Error { get; private set; }

        internal override bool Ready(float seconds)
        {
            if (request == 0) request = Begin(path);
            string text, error;
            if (!Poll(request, out text, out error)) return false;
            Text = text;
            Error = error;
            return true;
        }

        internal override void Release()
        {
            if (request != 0) Forget(request);
            request = 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern ulong Begin(string path);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool Poll(ulong request, out string text, out string error);
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void Forget(ulong request);
    }
}
