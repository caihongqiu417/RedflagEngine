using Redflag;

namespace Sandbox
{
    public class Player : Entity
    {
        public float Speed = 5.0f;

        //Test HotUpdate
        //public int TestValue = 3;

        void OnCreate()
        {
        }

        void OnUpdate(float ts)
        {
            Vector3 velocity = Vector3.Zero;

            if (Input.IsKeyDown(KeyCode.W))
                velocity.Y = 1.0f;
            else if (Input.IsKeyDown(KeyCode.S))
                velocity.Y = -1.0f;

            if (Input.IsKeyDown(KeyCode.A))
                velocity.X = -1.0f;
            else if (Input.IsKeyDown(KeyCode.D))
                velocity.X = 1.0f;

            //Internal Get
            var transform = GetComponent<TransformComponent>();

            //Internal Set
            transform.Translation += velocity * Speed * ts;
        }
    }
}