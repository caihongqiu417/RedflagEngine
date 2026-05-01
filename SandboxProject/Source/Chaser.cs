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

            float deltaX = targetPosition.X - selfPosition.X;
            float deltaY = targetPosition.Y - selfPosition.Y;
            float deltaZ = targetPosition.Z - selfPosition.Z;
            float distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;

            float stopDistanceSquared = StopDistance * StopDistance;
            if (distanceSquared <= stopDistanceSquared)
                return;

            float distance = (float)Math.Sqrt(distanceSquared);
            if (distance <= 0.0001f)
                return;

            float step = Speed * ts;
            if (step > distance)
                step = distance;

            float scale = step / distance;
            selfTransform.Translation = new Vector3(
                selfPosition.X + deltaX * scale,
                selfPosition.Y + deltaY * scale,
                selfPosition.Z + deltaZ * scale
            );
        }
    }
}
