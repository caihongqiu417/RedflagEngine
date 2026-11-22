using Redflag;

namespace Sandbox
{
    /* 维修站的任务文字与操作提示。显示位置、颜色和图片保存在 hud.yaml。 */
    public sealed class StationHud
    {
        private readonly ulong player;
        private readonly Hud view;
        private string lastTitle, lastObjective, lastHint, lastControls, lastEquipment;
        private bool showingHelp;
        private bool finished;
        private int lastLife = -1, lastCompanion = -1, lastKits = -1;
        private float lastMaximum = -1;

        public StationHud(ulong player)
        {
            this.player = player;
            view = new Hud(player);
        }

        public void Team(ulong actor, ulong companion, bool cannon, bool crouched, bool recovering)
        {
            int life = (int)GameWorld.Health(actor);
            float maximum = GameWorld.MaxHealth(actor);
            if (life != lastLife || maximum != lastMaximum)
            {
                view.SetText("life", "生命  " + life);
                view.SetMeter("health", maximum > 0 ? life / maximum : 0);
                lastLife = life;
                lastMaximum = maximum;
            }

            int ally = (int)GameWorld.Health(companion);
            int kits = GameWorld.ItemCount(actor, "medkit");
            if (ally != lastCompanion || kits != lastKits)
            {
                view.SetText("supplies", "瓦特  " + ally + "      医疗包  " + kits);
                lastCompanion = ally;
                lastKits = kits;
            }

            string equipment = cannon ? "臂炮" : "扫描器";
            if (crouched) equipment = "蹲伏";
            if (recovering) equipment = "落地中";
            if (lastEquipment != equipment)
            {
                view.SetText("equipment", equipment);
                lastEquipment = equipment;
            }
        }

        public void Show(string title, string objective, string hint, bool done)
        {
            bool showKeys = !done && Input.IsKeyDown(KeyCode.F1);
            if (showKeys != showingHelp)
            {
                view.SetText("help", showKeys ? "WASD 移动    Shift 奔跑    Ctrl 蹲伏\n空格 跳跃    右键 环视    左键 使用\n1 / 2 装备    H 医疗包    E 交互" : "");
                showingHelp = showKeys;
            }

            string controls = done ? "R   重新开始" : "T   呼叫瓦特     F1   操作帮助";
            if (lastTitle == title && lastObjective == objective && lastHint == hint &&
                lastControls == controls && finished == done) return;

            GameWorld.Hud(player, title, objective, hint, controls, done);
            lastTitle = title;
            lastObjective = objective;
            lastHint = hint;
            lastControls = controls;
            finished = done;
        }
    }
}
