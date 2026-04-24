using System;
using System.Runtime.CompilerServices;

namespace Redflag
{
    // 确保内存布局与 glm::vec3 一致（3个连续的float）
    public struct Vector3
    {
        public float X, Y, Z;

        public Vector3(float x, float y, float z)
        {
            X = x; Y = y; Z = z;
        }
    }

    // 用于标注哪些C#函数需要internalcall
    internal static class InternalCalls
    {
        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog(string text, int parameter);

        [MethodImplAttribute(MethodImplOptions.InternalCall)]
        internal static extern void NativeLog_Vector(ref Vector3 parameter, out Vector3 outResult);
    }

    public class Entity
    {
        public float FloatValue { get; set; }

        public Entity()
        {
            Console.WriteLine("Main Constructor");
            Log("Redflag:C# call C++", 12306);

            Vector3 pos = new Vector3(5, 2, 1);
            Vector3 result = new Vector3();
            Log(pos, out result);
            Console.WriteLine($"Normalized: ({result.X}, {result.Y}, {result.Z})");
        }

        public void PrintMessage()
        {
            Console.WriteLine("Hello World from C#!");
        }

        public void PrintInt(int value)
        {
            Console.WriteLine($"C# says: {value}");
        }

        public void PrintInts(int value1, int value2)
        {
            Console.WriteLine($"C# says: {value1} and {value2}");
        }

        public void PrintCustomMessage(string message)
        {
            Console.WriteLine($"C# says: {message}");
        }

        private void Log(string text, int parameter)
        {
            InternalCalls.NativeLog(text, parameter);
        }

        private void Log(Vector3 parameter, out Vector3 result)
        {
            InternalCalls.NativeLog_Vector(ref parameter, out result);
        }
    }
}