using Redflag;

namespace Sandbox
{
    /* 根据 Operator.glb 靴底顶点选出的脚尖、前掌和鞋跟。各点坐标相对它所在的骨头。 */
    static class OperatorSoles
    {
        public static void Bind(ulong actor)
        {
            const TraversalLimb left = TraversalLimb.LeftFoot, right = TraversalLimb.RightFoot;
            Add(actor,left,"toe.L",-.013112f,.160478f,-.015396f);
            Add(actor,left,"toe.L",.086669f,.065435f,-.015396f);
            Add(actor,left,"toe.L",-.013112f,.160478f,.016916f);
            Add(actor,left,"foot.L",.150273f,.105927f,.104177f);
            Add(actor,left,"foot.L",.013112f,-.041481f,.199076f);
            Add(actor,left,"foot.L",-.119700f,.069945f,.127342f);

            Add(actor,right,"toe.R",.012102f,.160478f,-.015083f);
            Add(actor,right,"toe.R",.012102f,.160478f,.017229f);
            Add(actor,right,"toe.R",-.051790f,.107770f,-.015117f);
            Add(actor,right,"foot.R",.021011f,-.041650f,.198813f);
            Add(actor,right,"foot.R",.120710f,.180387f,.056202f);
            Add(actor,right,"foot.R",-.144914f,.069776f,.127079f);
        }

        static void Add(ulong actor, TraversalLimb foot, string bone, float x, float y, float z)
        {
            Vector3 point = new Vector3(x,y,z);
            GameWorld.AddFootContact(actor,foot,bone,ref point);
        }
    }
}
