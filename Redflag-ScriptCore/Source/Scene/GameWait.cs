using System;
using System.Threading.Tasks;

namespace Redflag
{
    /* 协程每次交回一个等待条件。引擎在游戏线程检查它，条件满足后才继续往下运行。 */
    public abstract class GameWait
    {
        internal abstract bool Ready(float seconds);
        internal virtual void Release() { }

        private sealed class Condition : GameWait
        {
            private readonly Func<float, bool> check;
            internal Condition(Func<float, bool> check) { this.check = check; }
            internal override bool Ready(float seconds) { return check(seconds); }
        }

        public static GameWait Seconds(float seconds)
        {
            return new Condition(elapsed => { seconds -= elapsed; return seconds <= 0; });
        }

        /// <summary>任务成功、取消或失败都会结束等待。恢复后可检查状态，或调用 GetResult 读取结果。</summary>
        public static GameWait For(Task task) { return new Condition(elapsed => task.IsCompleted); }

        public static GameWait Until(Func<bool> condition) { return new Condition(elapsed => condition()); }
    }
}
