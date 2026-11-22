using Redflag;
using System.IO;

namespace RoutingTest
{
    public class Probe : Entity
    {
        bool held;
        bool walking;

        void OnCreate() { File.WriteAllText("mouse.txt", "ready\n"); }

        void OnUpdate(float seconds)
        {
            bool next = Input.IsMouseButtonDown(MouseButton.Left);
            if (next != held) File.AppendAllText("mouse.txt", next ? "down\n" : "up\n");
            held = next;
            bool forward = Input.IsKeyDown(KeyCode.W);
            if (forward != walking) Playtest.Record(Id,"walking",forward ? "begin" : "end");
            walking = forward;
            if (forward) {
                TransformComponent transform = GetComponent<TransformComponent>();
                Vector3 p = transform.Translation;
                p.X += seconds;
                transform.Translation = p;
            }
        }
    }
}
