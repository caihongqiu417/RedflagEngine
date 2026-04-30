namespace Redflag
{
    public class Entity
    {
        protected Entity() { ID = 0; }

        internal Entity(ulong id)
        {
            ID = id;
        }

        internal readonly ulong ID;

        //获取组件后，实际调用internal calls
        public T GetComponent<T>() where T : Component, new()
        {
            return new T() { Owner = this };
        }
    }
}
