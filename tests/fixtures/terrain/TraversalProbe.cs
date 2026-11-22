using System;
using System.Globalization;
using System.IO;
using Redflag;

namespace TerrainTest
{
    public class Walker : Entity
    {
        public int Expected;
        private int movingFrames;
        private bool captured;
        private const float Step = 1f / 30f;

        void OnCreate()
        {
            GameWorld.SetMovementRoot(Id,"root");
            GameWorld.SetTraversalLimb(Id,TraversalLimb.LeftFoot,"thigh.L","shin.L","foot.L");
            GameWorld.SetTraversalLimb(Id,TraversalLimb.RightFoot,"thigh.R","shin.R","foot.R");
            GameWorld.SetTraversalLimb(Id,TraversalLimb.LeftHand,"upper_arm.L","forearm.L","hand.L");
            GameWorld.SetTraversalLimb(Id,TraversalLimb.RightHand,"upper_arm.R","forearm.R","hand.R");
        }

        void OnUpdate(float seconds)
        {
            if (captured) return;
            Vector3 stride = new Vector3(0,0,.045f);
            TraversalMode mode = GameWorld.Traverse(Id,ref stride,.28f,Step,TraversalInput.Automatic);
            string clip = mode == TraversalMode.Climb ? "Station-Pickup" :
                mode == TraversalMode.Jump ? "jump_3_midair-cycle" : "walking_nogun-cycle";
            GameWorld.PlayClip(Id,clip);
            bool match = (int)mode == Expected;
            if (match) ++movingFrames;

            // 跨台阶那一帧就抓图。跳跃和攀爬留到身体已离开地面，再停下来看四肢。
            if (!match || movingFrames < (mode == TraversalMode.Step ? 1 : 12)) return;
            captured = true;
            GameWorld.Pose(Id,clip,.23f);
            Vector3 p=GetComponent<TransformComponent>().Translation;
            File.WriteAllText("capture.txt",String.Format(CultureInfo.InvariantCulture,"{0} {1:F3} {2:F3} {3:F3}",mode,p.X,p.Y,p.Z));
        }
    }
}
