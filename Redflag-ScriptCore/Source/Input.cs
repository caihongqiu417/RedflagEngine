namespace Redflag
{
    public class Input
    {
        public static bool IsKeyDown(KeyCode keyCode)
        {
            return InternalCalls.Input_IsKeyDown(keyCode);
        }

        public static bool IsMouseButtonDown(MouseButton button) { return InternalCalls.Input_IsMouseButtonDown((int)button); }

        public static void MousePosition(out float x, out float y) { InternalCalls.Input_MousePosition(out x, out y); }

        public static void CaptureCursor(bool capture) { InternalCalls.Input_CaptureCursor(capture); }
    }

    public enum MouseButton { Left, Right, Middle }
}
