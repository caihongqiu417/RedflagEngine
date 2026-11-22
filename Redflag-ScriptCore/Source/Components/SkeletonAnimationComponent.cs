namespace Redflag
{
    public class SkeletonAnimationComponent : Component
    {
        /// <summary>沿世界 X、Z 方向行走，到下次调用前保持这个方向。传零停步。角色需要已有待机与行走动作。</summary>
        public bool Move(Vector3 direction)
        {
            return InternalCalls.SkeletonAnimationComponent_Move(Owner.ID, ref direction);
        }

        /// <summary>转动实体，让它的正前方（本地 +Z）朝向指定的地面方向。</summary>
        public bool Face(Vector3 direction)
        {
            return InternalCalls.SkeletonAnimationComponent_Face(Owner.ID, ref direction);
        }
    }
}
