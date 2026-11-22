namespace Redflag
{
    /* 同一信号可唤醒多个脚本。创建等待之前发出的通知不计入这次等待。 */
    public sealed class GameSignal
    {
        private ulong version;

        public void Raise() { ++version; }

        public GameWait Next()
        {
            ulong seen = version;
            return GameWait.Until(() => seen != version);
        }
    }
}
