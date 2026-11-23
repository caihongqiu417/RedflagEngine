using Redflag;

namespace Sandbox
{
    /* 队友选择给自己用药时，播放与玩家按 H 相同的动作。 */
    public sealed class StationSelfCare : NpcAction
    {
        private readonly StationMedicine medicine = new StationMedicine();
        private bool started;

        public override InteractionResult Tick(ulong actor, ulong target, float seconds)
        {
            if (!started) {
                started = true;
                if (!Animator.Bind(actor, "assets/ai/watt.animator.yaml") || medicine.Start(actor, actor) != null)
                    return InteractionResult.Unavailable;
            }
            AnimationEvent notice;
            while (Animator.TryRead(actor, out notice)) medicine.OnNotice(notice);
            medicine.Update(seconds);
            if (medicine.Active) return InteractionResult.Running;
            return medicine.Completed ? InteractionResult.Completed : InteractionResult.Unavailable;
        }

        public override void Cancel(ulong actor, ulong target) { medicine.Cancel(); }
    }
}
