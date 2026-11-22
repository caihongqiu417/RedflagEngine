namespace Redflag
{
    /// <summary>行为树选中这个 C# 动作时，引擎为当前角色创建一个实例。实例字段可以保存这一次动作的进度。</summary>
    public abstract class NpcAction
    {
        /// <param name="seconds">自上一帧经过的秒数。返回 Running 后，引擎下一帧会继续调用同一个实例。</param>
        public abstract InteractionResult Tick(ulong actor, ulong target, float seconds);

        /// <summary>改令、角色消失或退出场景时，结束尚未完成的动作。可以在这里关掉脚本开启的效果。</summary>
        public virtual void Cancel(ulong actor, ulong target) {}
    }
}
