using Redflag;

namespace Sandbox
{
    /* E 瞄准药包可以拾取；维修和救援要一直按住。目标及握持位置来自场景。 */
    public sealed class StationInteraction
    {
        private const float PickupRange = 1.7f;
        private ulong actor, target;
        private string action;
        private bool pressedLastFrame, reaching, contactDue, took;
        public bool Active => target != 0;

        public void Reset()
        {
            if (reaching) GameWorld.EndContact(actor);
            actor = target = 0;
            action = null;
            reaching = contactDue = took = false;
        }

        public string Contact(ulong owner, AnimationEvent notice)
        {
            if (reaching && action == "pickup" && notice.State == action && notice.Name == "contact" &&
                notice.Phase == NotifyPhase.Point) contactDue = true;
            return null;
        }

        public string Update(StationPlayer player, TransformComponent pose, bool available, float seconds)
        {
            bool down = Input.IsKeyDown(KeyCode.E);
            bool pressed = down && !pressedLastFrame;
            pressedLastFrame = down;
            bool moving = Input.IsKeyDown(KeyCode.W) || Input.IsKeyDown(KeyCode.A) ||
                Input.IsKeyDown(KeyCode.S) || Input.IsKeyDown(KeyCode.D);
            if (!available || (Active && (action != "pickup" && !down || !reaching && moving))) { Reset(); return null; }

            if (!Active) {
                ulong focus = player.Focus;
                if (pressed && focus != 0 && GameWorld.CanCollect(player.Id,focus,PickupRange)) {
                    action = "pickup";
                    target = focus;
                } else if (down && GameWorld.Health(player.Watt.Id) <= 0 &&
                    (pose.Translation-player.Watt.GetComponent<TransformComponent>().Translation).Length() <= 2f) {
                    action = "revive";
                    target = player.Watt.Id;
                } else if (down && GameWorld.Health(player.Relay.Id) < GameWorld.MaxHealth(player.Relay.Id) &&
                    (pose.Translation-player.Relay.GetComponent<TransformComponent>().Translation).Length() <= 2f) {
                    action = "repair";
                    target = player.Relay.Id;
                } else return null;
                actor = player.Id;
            }

            if (!reaching) {
                if (action == "pickup" && !GameWorld.CanCollect(actor,target,PickupRange)) { Reset(); return null; }
                if (!GameWorld.ApproachContact(actor,target,action,seconds)) {
                    GameWorld.PlayLocomotion(actor,"walking_nogun-cycle");
                    return null;
                }
                if (!GameWorld.BeginContact(actor,target,action)) { Reset(); return null; }
                reaching = true;
                if (!Animator.Play(actor,action,true)) { Reset(); return null; }
                if (action != "pickup") GameWorld.Interact(actor,target,action,0f,true);
            }

            if (action == "pickup") {
                if (contactDue && !took && GameWorld.AtContact(actor)) {
                    took = GameWorld.Collect(actor,target,PickupRange);
                    Playtest.Record(actor,took ? "pickup_contact" : "pickup_missed",target.ToString());
                    if (!took) Reset();
                    return took ? "已拾取物品" : "已经够不到物品";
                }
                if (!Animator.IsPlaying(actor)) {
                    if (!took) Playtest.Record(actor,"pickup_missed",target.ToString());
                    Reset();
                }
                return null;
            }

            bool rescue = action == "revive";
            InteractionResult result = GameWorld.Interact(actor,target,action,seconds,false);
            if (result == InteractionResult.Running) return rescue ? "正在扶起队友" : "正在维修";
            if (result == InteractionResult.Completed) Playtest.Record(actor,action+"_contact",target.ToString());
            Reset();
            return result == InteractionResult.Completed ? (rescue ? "队友已恢复行动" : "供电已恢复") : null;
        }
    }
}
