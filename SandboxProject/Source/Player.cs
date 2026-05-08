using Redflag;

namespace Sandbox
{
    public class Player : Entity
    {
        public float Speed = 5.0f;

        //热更新测试
        public int TestValue = 3;

        void OnCreate()
        {
        }

        void OnUpdate(float ts)
        {
            Vector3 direction = Vector3.Zero;

            if (Input.IsKeyDown(KeyCode.W))
                direction.Z = 1.0f;
            else if (Input.IsKeyDown(KeyCode.S))
                direction.Z = -1.0f;

            if (Input.IsKeyDown(KeyCode.A))
                direction.X = -1.0f;
            else if (Input.IsKeyDown(KeyCode.D))
                direction.X = 1.0f;

            if (direction.LengthSquared() <= 0.0001f)
                return;

            var transform = GetComponent<TransformComponent>();

            //归一化方向向量，乘以速度和时间增量，得到位移增量，然后加到当前位移上
            transform.Translation += direction.Normalized() * Speed * ts;
        }
    }
}