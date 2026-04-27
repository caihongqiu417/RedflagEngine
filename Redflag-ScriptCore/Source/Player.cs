using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Sandbox
{
    public class Player
    {
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
