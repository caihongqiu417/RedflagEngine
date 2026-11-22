namespace Redflag
{
    public class PointLightComponent : Component
    {
        public float Intensity
        {
            get { return InternalCalls.PointLightComponent_GetIntensity(Owner.Id); }
            set { InternalCalls.PointLightComponent_SetIntensity(Owner.Id, value); }
        }
    }
}
