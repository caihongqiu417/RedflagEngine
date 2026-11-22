using System;
using Redflag;

namespace Workshop
{
    /* 维修台旁的操作员。场景决定行走速度，HUD 布局控制屏幕上那行提示的字号。 */
    public class Operator : Entity
    {
        public float WalkSpeed = 2f;
        public Entity Console;
        private Hud hud;
        private bool atPanel, opened;

        void OnCreate()
        {
            hud = new Hud(Id);
            hud.SetText("prompt", "W 走向阀门    E 打开阀门");
        }

        void OnUpdate(float seconds)
        {
            TransformComponent pose = GetComponent<TransformComponent>();
            float goal = Console.GetComponent<TransformComponent>().Translation.X;
            Vector3 position = pose.Translation;
            if (Input.IsKeyDown(KeyCode.W))
                position.X = Math.Min(goal, position.X + WalkSpeed * seconds);
            if (Input.IsKeyDown(KeyCode.S))
                position.X = Math.Max(0f, position.X - WalkSpeed * seconds);
            pose.Translation = position;

            // 两个模型各有宽度，靠到半米以内就能操作，不必走到阀门中心。
            bool nearby = goal - position.X <= 0.5f;
            if (nearby && !atPanel)
            {
                hud.SetText("prompt", "E 打开阀门");
                Playtest.Record(Id, "at_panel", "操作员已到达阀门。阀门仍关闭，按 E 即可打开。");
            }
            if (!nearby && atPanel && !opened)
                hud.SetText("prompt", "W 走向阀门    E 打开阀门");
            atPanel = nearby;

            if (nearby && !opened && Input.IsKeyDown(KeyCode.E))
            {
                opened = true;
                hud.SetText("prompt", "阀门已打开");
                Playtest.Record(Id, "valve_open", "已打开阀门");
            }
        }
    }
}
