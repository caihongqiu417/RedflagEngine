using System;
using Redflag;

namespace Sandbox
{
    /* 维修站的操作和任务进度。可交互的实体从场景属性指定。 */
    public class StationPlayer : Entity
    {
        public Entity Watt, Guard, Relay, Supply, Exit, Camera;
        public float MoveSpeed = 3f;
        // 玩家进入闸口后，瓦特走到闸口附近也算到齐。距离以米计，可在场景中调整。
        public float ExitRadius = 1.6f;
        public float TeamExitRadius = 3.5f;
        public bool Crouched => motion.Crouched;
        public bool Airborne => motion.Airborne;
        public bool Running => motion.Running;
        public bool LandingRecovery => motion.Recovering;
        public bool Cannon => equipment.Cannon;
        public ulong Focus => equipment.Focus;

        private TransformComponent pose, camera;
        private readonly StationMotion motion = new StationMotion();
        private readonly StationEquipment equipment = new StationEquipment();
        private ulong usingTarget;
        private float downSeconds, noticeSeconds;
        private bool usedKit, won, lost;
        private int rescues;
        private string notice = "";
        private StationHud hud;

        void OnCreate()
        {
            Animator.Bind(Id,"assets/ai/watt.animator.yaml");
            hud = new StationHud(Id);
            SetOperatorLimbs(Id);
            SetOperatorLimbs(Watt.Id);
            pose = GetComponent<TransformComponent>();
            camera = Camera.GetComponent<TransformComponent>();
            Vector3 green = new Vector3(.45f,.95f,.65f), red = new Vector3(1,.37f,.24f);

            GameWorld.Label(Watt.Id, ref green, 2.18f);
            GameWorld.Label(Guard.Id, ref red, 2.42f);
        }

        static void SetOperatorLimbs(ulong actor)
        {
            GameWorld.SetMovementRoot(actor,"root");
            GameWorld.SetTraversalLimb(actor,TraversalLimb.LeftFoot,"thigh.L","shin.L","foot.L");
            GameWorld.SetTraversalLimb(actor,TraversalLimb.RightFoot,"thigh.R","shin.R","foot.R");
            GameWorld.SetTraversalLimb(actor,TraversalLimb.LeftHand,"upper_arm.L","forearm.L","hand.L");
            GameWorld.SetTraversalLimb(actor,TraversalLimb.RightHand,"upper_arm.R","forearm.R","hand.R");
        }

        void OnUpdate(float seconds)
        {
            if (won || lost) return;
            noticeSeconds -= seconds;

            // 倒地后给瓦特留出十二秒救援。恢复生命时，移动和操作立即恢复。
            if (GameWorld.Health(Id) <= 0) {
                if (downSeconds == 0) Playtest.Record(Id, "player_down", "玩家倒地");
                hud.Team(Id, Watt.Id, equipment.Cannon, false, false);
                usingTarget = 0;
                downSeconds += seconds;
                lost = downSeconds >= 12f || rescues >= 2 || GameWorld.Health(Watt.Id) <= 0;
                if (lost) Playtest.Record(Id, "mission_failed", "维修班组失去联系");
                GameWorld.PlayClip(Id, "flinch_heavy");
                Display(lost ? "任务失败" : "等待队友救援", lost ? "维修班组失去联系。" : "瓦特可以靠近并扶起你。",
                    "救援剩余 " + Math.Max(0, (int)Math.Ceiling(12f - downSeconds)) + " 秒");
                return;
            }

            if (downSeconds > 0)
            {
                ++rescues;
                Playtest.Record(Id, "player_revived", "玩家恢复行动");
            }
            downSeconds = 0;
            string feedback = equipment.Update(this,camera,seconds);
            if (feedback != null) Notice(feedback);
            motion.Update(this,pose,camera.Rotation.Y,Input.IsMouseButtonDown(MouseButton.Left),MoveSpeed,seconds);
            motion.Animate(Id,equipment);
            UseItems(seconds);

            bool power = GameWorld.Health(Relay.Id) >= 100;
            bool atExit = power && Distance(Exit) <= ExitRadius;
            bool allyDown = GameWorld.Health(Watt.Id) <= 0;
            if (atExit && !allyDown)
            {
                Vector3 exit = Exit.GetComponent<TransformComponent>().Translation;
                Vector3 companion = Watt.GetComponent<TransformComponent>().Translation;
                won = (companion - exit).Length() <= TeamExitRadius;
            }
            if (won) Playtest.Record(Id, "mission_complete", "玩家与瓦特已从西侧闸口撤离");
            string hint = noticeSeconds > 0 ? notice : "";
            if (hint == "" && Supply != null
                && equipment.Focus == Supply.Id && Distance(Supply) < 1.7f)
                hint = "E   拾取医疗包";
            if (hint == "" && !power && Distance(Relay) < 2f) hint = "按住 E   维修发电机";

            hud.Team(Id, Watt.Id, equipment.Cannon, motion.Crouched, motion.Recovering);
            string objective = power ? "带瓦特返回西侧出口" : "到东侧铁皮棚恢复供电";
            if (atExit && !won)
                objective = allyDown ? "瓦特倒下了，靠近后按住 E 救援" : "等瓦特到闸口，按 T 可以呼叫它";
            Display(won ? "任务完成" : "潮汐维修站", won ? "维修班组已撤离" : objective, hint);
        }

        // ---- 补给、救援与设备维修 ----

        void UseItems(float seconds)
        {
            bool heal = Input.IsKeyDown(KeyCode.H);
            if (heal && !usedKit) Notice(GameWorld.Heal(Id,"medkit",50) ? "已使用医疗包" : "当前无法使用医疗包");
            usedKit = heal;
            if (!Input.IsKeyDown(KeyCode.E)) { usingTarget = 0; return; }

            if (equipment.Focus != 0 && GameWorld.Collect(Id,equipment.Focus,1.7f)) {
                Notice("已拾取医疗包");
                return;
            }
            bool rescue = GameWorld.Health(Watt.Id) <= 0 && Distance(Watt) < 2f;
            Entity target = rescue ? Watt : Relay;
            InteractionResult result = GameWorld.Interact(Id, target.Id, rescue ? "revive" : "repair", seconds, usingTarget != target.Id);
            usingTarget = result == InteractionResult.Running ? target.Id : 0;
            if (result == InteractionResult.Completed) Notice(rescue ? "瓦特已恢复行动" : "供电已恢复");
            if (result == InteractionResult.Running) Notice(rescue ? "正在扶起瓦特" : "正在维修");
        }

        float Distance(Entity other) { return (pose.Translation - other.GetComponent<TransformComponent>().Translation).Length(); }

        void Notice(string message) { notice = message; noticeSeconds = 1.5f; }

        void Display(string title, string objective, string status)
        {
            hud.Show(title, objective, status, won || lost);
        }
    }
}
