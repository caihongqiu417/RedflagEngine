using System.Runtime.CompilerServices;

namespace Redflag
{
    internal static class InternalCalls
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog(string text, int parameter);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog_Vector(ref Vector3 parameter, out Vector3 outResult);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void TransformComponent_GetTranslation(ulong entityID, out Vector3 translation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void TransformComponent_SetTranslation(ulong entityID, ref Vector3 translation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool Input_IsKeyDown(KeyCode keycode);
    }
}
