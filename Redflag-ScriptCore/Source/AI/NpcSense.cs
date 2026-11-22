using System.Runtime.CompilerServices;

namespace Redflag
{
    public struct EnvironmentFact
    {
        public string Id, Description;
    }

    public static class NpcSense
    {
        /// <summary>读取角色当前位置的区域与灯具。结果是调用时的快照，角色移动后可重新读取。</summary>
        public static EnvironmentFact[] GetEnvironment(ulong actor)
        {
            string[] pairs = ReadEnvironment(actor);
            EnvironmentFact[] facts = new EnvironmentFact[pairs.Length / 2];
            for (int i = 0; i < facts.Length; ++i)
            {
                facts[i].Id = pairs[i * 2];
                facts[i].Description = pairs[i * 2 + 1];
            }
            return facts;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string[] ReadEnvironment(ulong actor);
    }
}
