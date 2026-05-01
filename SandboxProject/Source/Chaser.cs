using System;
using Redflag;

namespace Sandbox
{
    public class Chaser : Entity
    {
        public Entity Target;
        public float Speed = 3.0f;
        public float StopDistance = 0.1f;

        void OnUpdate(float ts)
        {
            if (Target == null)
                return;

            TransformComponent selfTransform = GetComponent<TransformComponent>();
            TransformComponent targetTransform = Target.GetComponent<TransformComponent>();

            Vector3 selfPosition = selfTransform.Translation;
            Vector3 targetPosition = targetTransform.Translation;
            Vector3 toTarget = targetPosition - selfPosition;

            float distance = toTarget.Length();
            if (distance <= StopDistance || distance <= 0.0001f)
                return;

            Vector3 direction = toTarget / distance;

            float step = Speed * ts;
            if (step > distance)
                step = distance;

            selfTransform.Translation = selfPosition + direction * step;

            // 在XZ平面上旋转实体，使其面向目标
            if (direction.X * direction.X + direction.Z * direction.Z > 0.0001f)
            {
                Vector3 rotation = selfTransform.Rotation;
                rotation.Y = (float)Math.Atan2(direction.X, direction.Z);
                selfTransform.Rotation = rotation;
            }
        }
    }
}
