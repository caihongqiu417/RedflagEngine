using System;
using Redflag;

namespace Workshop
{
    /* 工人停下来读设备上的仪表。每一次检查都从零开始，另一名工人的进度独立保存。 */
    public class ReadGauge : NpcAction
    {
        private float elapsed;

        public override InteractionResult Tick(ulong actor, ulong target, float seconds)
        {
            elapsed += seconds;
            if (elapsed < 0.5f) return InteractionResult.Running;

            Console.WriteLine("Gauge reading {0} = {1}", target, GameWorld.Health(target));
            return InteractionResult.Completed;
        }

        public override void Cancel(ulong actor, ulong target)
        {
            Console.WriteLine("Gauge reading cancelled for {0}", target);
        }
    }
}
