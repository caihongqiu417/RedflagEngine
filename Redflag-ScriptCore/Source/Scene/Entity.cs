namespace Redflag
{
    public partial class Entity
    {
        protected Entity() { ID = 0; }

        internal Entity(ulong id)
        {
            ID = id;
        }

        internal readonly ulong ID;
        public ulong Id => ID;

        public T As<T>() where T : Entity { return InternalCalls.Entity_GetScript(ID) as T; }

        public static Entity Find(ulong id)
        {
            return InternalCalls.Entity_Exists(id) ? new Entity(id) : null;
        }

        //获取组件后，实际调用internal calls
        public T GetComponent<T>() where T : Component, new()
        {
            return new T() { Owner = this };
        }
    }
}
