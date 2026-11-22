using System;
using System.Collections.Generic;

namespace Redflag
{
    public sealed class Coroutine
    {
        private readonly IEnumerator<GameWait> steps;
        private GameWait waiting;
        private bool running;

        internal Coroutine(IEnumerator<GameWait> steps) { this.steps = steps; }

        public bool IsDone { get; private set; }
        public bool IsCancelled { get; private set; }
        public Exception Error { get; private set; }

        public void Cancel()
        {
            if (IsDone) return;
            IsCancelled = true;
            // 脚本可以在当前步骤里取消自己。等 MoveNext 返回后再运行迭代器的 finally。
            if (!running) Finish();
        }

        internal void Tick(float seconds)
        {
            if (IsDone) return;
            running = true;
            bool finished = false;
            try
            {
                if (waiting != null && !waiting.Ready(seconds)) return;
                waiting?.Release();
                waiting = null;
                if (IsCancelled) return;
                if (steps.MoveNext()) waiting = steps.Current;
                else finished = true;
            }
            catch (Exception error) { Report(error); finished = true; }
            finally
            {
                running = false;
                if (finished || IsCancelled) Finish();
            }
        }

        private void Finish()
        {
            IsDone = true;
            try { waiting?.Release(); }
            finally
            {
                waiting = null;
                try { steps.Dispose(); }
                catch (Exception error) { Report(error); }
            }
        }

        private void Report(Exception error)
        {
            Error = error;
            Console.Error.WriteLine(error);
        }
    }
}
