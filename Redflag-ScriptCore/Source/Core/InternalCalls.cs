using System.Runtime.CompilerServices;

namespace Redflag
{
    internal static class InternalCalls
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool TerrainComponent_GetSurface(ulong id, float x, float z, out float height, out Vector3 normal);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern float PointLightComponent_GetIntensity(ulong id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void PointLightComponent_SetIntensity(ulong id, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool Entity_Exists(ulong id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Entity Entity_GetScript(ulong id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool SkeletonAnimationComponent_Move(ulong id, ref Vector3 direction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool SkeletonAnimationComponent_Face(ulong id, ref Vector3 direction);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog(string text, int parameter);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog_Vector(ref Vector3 parameter, out Vector3 outResult);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void TransformComponent_GetTranslation(ulong entityID, out Vector3 translation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void TransformComponent_SetTranslation(ulong entityID, ref Vector3 translation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void TransformComponent_GetRotation(ulong entityID, out Vector3 rotation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void TransformComponent_SetRotation(ulong entityID, ref Vector3 rotation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool Input_IsKeyDown(KeyCode keycode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool Input_IsMouseButtonDown(int button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void Playtest_Record(ulong entity, string kind, string text);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void Input_MousePosition(out float x, out float y);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void Input_CaptureCursor(bool capture);
    }
}
