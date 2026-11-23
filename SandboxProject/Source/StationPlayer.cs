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
        public bool Punching => melee.Active;
        public ulong Focus => equipment.Focus;

        private TransformComponent pose, camera;
        private readonly StationMotion motion = new StationMotion();
        private readonly StationEquipment equipment = new StationEquipment();
        private readonly StationMelee melee = new StationMelee();
        private readonly StationInteraction interaction = new StationInteraction();
        private readonly StationMedicine medicine = new StationMedicine();
        private float downSeconds, noticeSeconds;
        private bool healHeld, giveHeld, gettingUp, won, lost;
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
            OperatorSoles.Bind(actor);
        }

        void OnUpdate(float seconds)
        {
            if (won || lost) return;
            noticeSeconds -= seconds;
            bool punchPressed = melee.ReadPress();
            bool heal = Input.IsKeyDown(KeyCode.H), give = Input.IsKeyDown(KeyCode.G);
            bool healPressed = heal && !healHeld, givePressed = give && !giveHeld;
            healHeld = heal;
            giveHeld = give;

            // 倒地后有十二秒等队友来救。站起动作结束，玩家才重新接手移动。
            if (GameWorld.Health(Id) <= 0) {
                if (melee.Active) melee.End(Id, true);
                if (downSeconds == 0) {
                    medicine.Cancel();
                    gettingUp = false;
                    Animator.Play(Id, "down", true);
                    Playtest.Record(Id, "player_down", "玩家倒地");
                }
                hud.Team(Id, Watt.Id, equipment.Cannon, false, false);
                interaction.Reset();
                downSeconds += seconds;
                lost = downSeconds >= 12f || rescues >= 2 || GameWorld.Health(Watt.Id) <= 0;
                if (lost) Playtest.Record(Id, "mission_failed", "维修班组失去联系");
                Display(lost ? "任务失败" : "等待队友救援", lost ? "维修班组失去联系。" : "瓦特可以靠近并扶起你。",
                    "救援剩余 " + Math.Max(0, (int)Math.Ceiling(12f - downSeconds)) + " 秒");
                return;
            }

            if (downSeconds > 0)
            {
                ++rescues;
                gettingUp = Animator.Play(Id, "getup", true);
                Playtest.Record(Id, "player_revived", "玩家恢复行动");
            }
            downSeconds = 0;

            AnimationEvent animationEvent;
            while (Animator.TryRead(Id, out animationEvent)) {
                motion.OnNotice(animationEvent);
                medicine.OnNotice(animationEvent);
                string contact = interaction.Contact(Id, animationEvent);
                if (contact != null) Notice(contact);
                if (melee.Active && animationEvent.State=="punch" && animationEvent.Name=="Contact" && animationEvent.Phase==NotifyPhase.Point)
                    Notice(melee.Contact(Id, Guard.Id, pose));
            }
            if (melee.Active && !Animator.IsPlaying(Id)) melee.End(Id, false);
            if (gettingUp && !Animator.IsPlaying(Id)) gettingUp = false;
            if (punchPressed && !melee.Active && !motion.Airborne && !motion.Landing && !Input.IsKeyDown(KeyCode.LeftControl)
                && !gettingUp && !medicine.Active && !interaction.Active && equipment.ChangeTime<=0 && equipment.ShotTime<=0)
                melee.Begin(Id, pose, camera.Rotation.Y);

            bool freeHands = !melee.Active && !interaction.Active && !gettingUp && !motion.Airborne && !motion.Landing;
            HandleMedicine(healPressed, givePressed, freeHands, seconds);

            string feedback = equipment.Update(this,camera,seconds,!melee.Active && !interaction.Active && !medicine.Active && !gettingUp);
            if (feedback != null) Notice(feedback);
            feedback = interaction.Update(this, pose, !melee.Active && !medicine.Active && !gettingUp && !motion.Airborne && !motion.Landing, seconds);
            if (feedback != null) Notice(feedback);

            bool busy = melee.Active || medicine.Active || interaction.Active || gettingUp;
            motion.Update(this,pose,camera.Rotation.Y,Input.IsMouseButtonDown(MouseButton.Left),MoveSpeed,seconds,busy);
            if (!busy) motion.Animate(Id,equipment);

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

        void HandleMedicine(bool heal, bool give, bool available, float seconds)
        {
            // 递药前允许玩家用方向键离开；开始伸手后由动作表决定交接时间。
            if (medicine.Approaching && (give || Input.IsKeyDown(KeyCode.W) || Input.IsKeyDown(KeyCode.A)
                || Input.IsKeyDown(KeyCode.S) || Input.IsKeyDown(KeyCode.D))) {
                medicine.Cancel();
                return;
            }
            string message = null;
            if (available && !medicine.Active) {
                if (heal) message = medicine.Start(Id, Id);
                else if (give) message = Distance(Watt) <= 2f ? medicine.Start(Id, Watt.Id) : "靠近瓦特后按 G 递药";
            }
            Vector3 previous = pose.Translation;
            if (message == null) message = medicine.Update(seconds);
            if (medicine.Approaching) {
                if ((pose.Translation - previous).LengthSquared() > 0)
                    GameWorld.PlayLocomotion(Id, equipment.Cannon ? "walking_gun-cycle" : "walking_nogun-cycle");
                else GameWorld.PlayClip(Id, "Idle-cycle");
            }
            if (message != null) Notice(message);
        }

        float Distance(Entity other) { return (pose.Translation - other.GetComponent<TransformComponent>().Translation).Length(); }

        void Notice(string message) { notice = message; noticeSeconds = 1.5f; }

        void Display(string title, string objective, string status)
        {
            hud.Show(title, objective, status, won || lost);
        }
    }
}
