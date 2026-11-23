using Redflag;

namespace Sandbox
{
    /* 用药和递药共用同一只包。伸手前检查能不能做，接触时才扣库存。 */
    public sealed class StationMedicine
    {
        private const float TransferRange = 2f;
        private ulong owner, recipient;
        private bool reaching, contactDue, attempted;
        public bool Active => owner != 0;
        public bool Completed { get; private set; }
        private bool Giving => owner != recipient;
        private string State => Giving ? "give" : "heal";

        public string Start(ulong actor, ulong target)
        {
            Completed = false;
            if (GameWorld.ItemCount(actor, "medkit") == 0) return "背包里没有医疗包";
            if (actor == target && GameWorld.Health(actor) >= GameWorld.MaxHealth(actor)) return "生命已满";
            if (actor != target && GameWorld.Health(target) <= 0) return "按住 E 先扶起队友";

            owner = actor;
            recipient = target;
            reaching = contactDue = attempted = false;
            if (!Giving && !Reach()) return "暂时无法使用医疗包";
            return null;
        }

        private bool Reach()
        {
            reaching = GameWorld.BeginContact(owner, recipient, State);
            if (!reaching || !Animator.Play(owner, State, true)) {
                Cancel();
                return false;
            }
            Playtest.Record(owner, State + "_begin", recipient.ToString());
            return true;
        }

        public void OnNotice(AnimationEvent notice)
        {
            if (reaching && notice.State == State && notice.Name == "contact" && notice.Phase == NotifyPhase.Point)
                contactDue = true;
        }

        public string Update(float seconds)
        {
            if (!Active) return null;
            if (!reaching) {
                if (GameWorld.Health(recipient) <= 0) { Cancel(); return "队友已经倒下"; }
                if (!GameWorld.ApproachContact(owner, recipient, "give", seconds)) return null;
                if (!Reach()) return "暂时无法递药";
            }

            string result = null;
            if (contactDue && !attempted && GameWorld.AtContact(owner)) {
                attempted = true;
                Completed = Giving ? GameWorld.Give(owner, recipient, "medkit", 1, TransferRange)
                                   : GameWorld.Heal(owner, "medkit", 50);
                Playtest.Record(owner, State + (Completed ? "_contact" : "_missed"), recipient.ToString());
                result = Completed ? (Giving ? "已把医疗包递给队友" : "已使用医疗包") : "医疗包没有使用";
            }
            if (!Animator.IsPlaying(owner)) {
                if (!attempted) {
                    Playtest.Record(owner, State + "_missed", recipient.ToString());
                    result = Giving ? "未能把医疗包交到对方手上" : "未能把医疗包放到胸前";
                }
                Cancel();
            }
            return result;
        }

        public void Cancel()
        {
            if (reaching) GameWorld.EndContact(owner);
            owner = recipient = 0;
            reaching = contactDue = false;
        }

        public bool Approaching => Active && !reaching;
    }
}
