using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Redflag;

namespace Sandbox
{
    public class Player:Entity
    {
        public float Speed = 5.0f;

        void OnCreate()
        {
            Console.WriteLine("Player.OnCreate");
        }


        void OnUpdate(float ts)
        {
            Console.WriteLine($"Player.OnUpdate:{ts}");
        }
    }
}
