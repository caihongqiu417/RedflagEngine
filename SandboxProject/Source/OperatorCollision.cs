using System;
using Redflag;

namespace Sandbox
{
    /* 玩家和瓦特用同一副骨架。站立尺寸从各自的场景组件读取，卧姿跟随身体倒下或撑起。 */
    class OperatorCollision
    {
        readonly Entity actor;
        readonly CapsuleCollider3DComponent collider;
        readonly CapsuleShape standing;
        readonly bool hasCapsule;
        bool prone, crouched;

        public OperatorCollision(Entity owner)
        {
            actor = owner;
            collider = owner.GetComponent<CapsuleCollider3DComponent>();
            hasCapsule = collider.TryGetShape(out standing);
        }

        public void Crouch(bool value, float height)
        {
            if (value == crouched || prone) return;
            crouched = value;
            if (!hasCapsule) {
                GameWorld.BodyHeight(actor.Id, value ? height : 1.84f); // 早期维修站角色的站立盒高 1.84 米。
                return;
            }
            CapsuleShape shape = standing;
            if (value) {
                shape.Height = Math.Max(height, 2f * shape.Radius);
                shape.Center.Y -= (standing.Height - shape.Height) * .5f; // 保持胶囊底面在原来的高度。
            }
            collider.TrySetShape(shape);
        }

        public void UpdateDown(bool down, CapsuleShape lying)
        {
            if (!hasCapsule || down == prone) return;

            // 取动画当前的胸骨和骨盆。躯干更接近平放时横置胶囊，撑起来后再转回竖直。
            Vector3 origin = Vector3.Zero, hips, chest;
            if (!GameWorld.Socket(actor.Id, "hips", ref origin, out hips) ||
                !GameWorld.Socket(actor.Id, "chest", ref origin, out chest)) return;
            Vector3 spine = chest - hips;
            bool horizontal = spine.X * spine.X + spine.Z * spine.Z > spine.Y * spine.Y;
            if (down != horizontal) return;

            if (collider.TrySetShape(down ? lying : standing)) {
                crouched = false;
                prone = down;
            }
        }
    }
}
