namespace Redflag
{
    public class TransformComponent : Component
    {
        public Vector3 Translation
        {
            get
            {
                InternalCalls.TransformComponent_GetTranslation(Owner.ID, out Vector3 result);
                return result;
            }
            set
            {
                InternalCalls.TransformComponent_SetTranslation(Owner.ID, ref value);
            }
        }

        public Vector3 Rotation
        {
            get
            {
                InternalCalls.TransformComponent_GetRotation(Owner.ID, out Vector3 result);
                return result;
            }
            set
            {
                InternalCalls.TransformComponent_SetRotation(Owner.ID, ref value);
            }
        }
    }
}
