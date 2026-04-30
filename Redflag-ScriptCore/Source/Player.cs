using Redflag;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using static System.Runtime.CompilerServices.RuntimeHelpers;

namespace Sandbox
{
    public class Player:Entity
    {
        public float Speed = 5.0f;


        //基类已经将UUID写入！！

        void OnCreate()
        {
            Console.WriteLine("Player.OnCreate");
        }

        void OnUpdate(float ts)
        {
            Console.WriteLine($"Player.OnUpdate:{ts}");

            float speed = 10.0f;
            Vector3 velocity = Vector3.Zero;

            if (Input.IsKeyDown(KeyCode.W))
                velocity.Y = 1.0f;
            else if (Input.IsKeyDown(KeyCode.S))
                velocity.Y = -1.0f;

            if (Input.IsKeyDown(KeyCode.A))
                velocity.X = -1.0f;
            else if (Input.IsKeyDown(KeyCode.D))
                velocity.X = 1.0f;

            velocity*=speed;

            // 1. getter：从 C++ ECS 拷贝当前位置到栈上
            Vector3 translation = Translation;

            // 2. 修改栈上的副本
            translation += velocity * ts;

            // 3. setter：把修改后的副本写回 C++ ECS
            Translation = translation;
        }
    }
}
