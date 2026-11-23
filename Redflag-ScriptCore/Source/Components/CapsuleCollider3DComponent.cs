using System.Runtime.InteropServices;

namespace Redflag
{
    public enum CapsuleAxis { X, Y, Z }

    /// <summary>Center 在模型坐标中。Height 沿 Axis 从一个圆帽顶点量到另一个，包含两个半球。</summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct CapsuleShape
    {
        public CapsuleAxis Axis;
        public Vector3 Center;
        public float Radius, Height;
    }

    public class CapsuleCollider3DComponent : Component
    {
        public bool TryGetShape(out CapsuleShape shape)
        {
            return InternalCalls.CapsuleCollider3DComponent_Get(Owner.Id, out shape);
        }

        /// <summary>在下次物理同步时一起更换方向、尺寸和中心。总长至少为直径。</summary>
        public bool TrySetShape(CapsuleShape shape)
        {
            return InternalCalls.CapsuleCollider3DComponent_Set(Owner.Id, ref shape);
        }
    }
}
