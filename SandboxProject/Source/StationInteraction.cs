using System;
using Redflag;

namespace Sandbox
{
    /* E 对准物品时拾取，按住时可以修理设备或扶起队友。 */
    public sealed class StationInteraction
    {
        private const float PickupRange = 1.7f;
        private ulong pickup, heldTarget;
        private bool pressedLastFrame;
        public bool Active => pickup != 0 || heldTarget != 0;

        public void Reset()
        {
            pickup = heldTarget = 0;
        }

        public string Contact(ulong actor, AnimationEvent notice)
        {
            if (pickup == 0 || notice.State != "pickup" || notice.Name != "contact" || notice.Phase != NotifyPhase.Point)
                return null;

            // 伸手期间，物品可能已经被瓦特拿走。只在手落到物品旁时改库存。
            bool taken = GameWorld.Collect(actor, pickup, PickupRange);
            Playtest.Record(actor, taken ? "pickup_contact" : "pickup_missed", pickup.ToString());
            return taken ? "已拾取物品" : "已经够不到物品";
        }

        public string Update(StationPlayer player, TransformComponent pose, bool available, float seconds)
        {
            bool down = Input.IsKeyDown(KeyCode.E);
            bool pressed = down && !pressedLastFrame;
            pressedLastFrame = down;
            if (pickup != 0) {
                if (!Animator.IsPlaying(player.Id)) pickup = 0;
                return null;
            }
            if (!down || !available) {
                heldTarget = 0;
                return null;
            }

            ulong focus = player.Focus;
            if (pressed && focus != 0 && GameWorld.CanCollect(player.Id, focus, PickupRange)) {
                if (Animator.Play(player.Id, "pickup", true)) {
                    pickup = focus;
                    Face(pose, Entity.Find(focus));
                }
                return null;
            }

            bool rescue = GameWorld.Health(player.Watt.Id) <= 0 &&
                (pose.Translation - player.Watt.GetComponent<TransformComponent>().Translation).Length() < 2f;
            Entity target = rescue ? player.Watt : player.Relay;
            string action = rescue ? "revive" : "repair";
            bool start = heldTarget != target.Id;
            InteractionResult result = GameWorld.Interact(player.Id, target.Id, action, seconds, start);
            heldTarget = result == InteractionResult.Running ? target.Id : 0;
            if (start && heldTarget != 0) {
                Face(pose, target);
                Animator.Play(player.Id, action, true);
            }
            if (result == InteractionResult.Running) return rescue ? "正在扶起瓦特" : "正在维修";
            if (result == InteractionResult.Completed) return rescue ? "瓦特已恢复行动" : "供电已恢复";
            return null;
        }

        static void Face(TransformComponent pose, Entity target)
        {
            Vector3 offset = target.GetComponent<TransformComponent>().Translation - pose.Translation;
            pose.Rotation = new Vector3(0, (float)Math.Atan2(offset.X, offset.Z), 0);
        }
    }
}
