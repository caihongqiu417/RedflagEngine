using System;
using System.Runtime.CompilerServices;

namespace Redflag
{
    // 确保内存布局与 glm::vec3 一致（12)
    // glm::vec3 
    public struct Vector3
    {
        public float X, Y, Z;

        public static Vector3 Zero=>new Vector3(0, 0, 0);

        public Vector3(float x, float y, float z)
        {
            X = x; Y = y; Z = z;
        }

        public static Vector3 operator +(Vector3 a ,Vector3 b)
        {
            return new Vector3(a.X+b.X,a.Y+b.Y,a.Z+b.Z);
        }
        public static Vector3 operator*(Vector3 vector,float scalar)
        {
            return new Vector3(vector.X * scalar, vector.Y * scalar, vector.Z * scalar);
        }
    }

    // 用于标注哪些C#函数需要internalcall调用引擎内部函数（ C++ ）
    internal static class InternalCalls
    {
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog(string text, int parameter);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog_Vector(ref Vector3 parameter, out Vector3 outResult);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern void Entity_GetTranslation(ulong entityID,out Vector3 translation);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern void Entity_SetTranslation(ulong entityID,ref Vector3 translation);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern bool Input_IsKeyDown(KeyCode keycode);
    }


    public class Entity
    {
        protected Entity() { ID = 0; }

        //实体真正数据只能在C++分配，C#只是包装
        internal Entity(ulong id)
        {
            ID = id;  //存入UUID
        }
        private readonly ulong ID;

        public Vector3 Translation
        {
            get
            {
                InternalCalls.Entity_GetTranslation(ID,out Vector3 translation);
                return translation;
            }
            set
            {
                InternalCalls.Entity_SetTranslation(ID, ref value);
            }
        }
    }
}