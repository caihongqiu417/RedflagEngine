namespace Redflag
{
    public class TerrainComponent : Component
    {
        /// <summary>读取地形三角面上的高度和朝上的法线。x、z 与返回的高度都用地形自身的米制坐标。</summary>
        /// <returns>位置落在高度图内时返回 true。</returns>
        public bool TryGetSurface(float x, float z, out float height, out Vector3 normal)
        {
            return InternalCalls.TerrainComponent_GetSurface(Owner.Id, x, z, out height, out normal);
        }
    }
}
