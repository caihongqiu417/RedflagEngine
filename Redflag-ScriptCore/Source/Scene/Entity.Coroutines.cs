using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Redflag
{
    public partial class Entity
    {
        private List<Coroutine> routines;
        private bool stopped;

        /// <summary>从下次脚本更新开始。yield return null 等一帧，停止播放或删除实体会取消它。</summary>
        protected Coroutine StartCoroutine(IEnumerator<GameWait> steps)
        {
            if (stopped) throw new InvalidOperationException("This script has stopped.");
            if (steps == null) throw new ArgumentNullException(nameof(steps));
            if (routines == null) routines = new List<Coroutine>();
            if (routines.Count == 0) SetCoroutinesActive(ID, true);
            Coroutine routine = new Coroutine(steps);
            routines.Add(routine);
            return routine;
        }

        private void AdvanceCoroutines(float seconds)
        {
            // 在这个步骤里启动的另一条协程留到下一帧，保持同一实体内的调用顺序。
            int count = routines.Count;
            for (int i = 0; i < count && !stopped; ++i) routines[i].Tick(seconds);
            routines.RemoveAll(routine => routine.IsDone);
            if (!stopped && routines.Count == 0) SetCoroutinesActive(ID, false);
        }

        private void StopCoroutines()
        {
            stopped = true;
            if (routines != null)
                foreach (Coroutine routine in routines) routine.Cancel();
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void SetCoroutinesActive(ulong id, bool active);
    }
}
