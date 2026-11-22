using System.Globalization;
using System.IO;
using Redflag;

namespace PlayTest
{
    public class Walker : Entity
    {
        public Entity Partner;
        public Vector3 Direction;
        int updates;
        bool valid;

        void OnCreate()
        {
            SkeletonAnimationComponent body = GetComponent<SkeletonAnimationComponent>();
            valid = Partner != null && Find(Partner.Id) != null && Find(999999) == null;
            valid = body.Face(Direction) && body.Move(Direction) && valid;
            Record("create");
        }

        void OnUpdate(float seconds)
        {
            if (++updates == 4) Record("update");
        }

        void OnDestroy()
        {
            Record("destroy");
        }

        void Record(string stage)
        {
            TransformComponent transform = GetComponent<TransformComponent>();
            Vector3 point = transform.Translation;
            File.AppendAllText("play.csv", string.Format(CultureInfo.InvariantCulture,
                "{0},{1},{2},{3:R},{4:R},{5:R}\n", Id, stage, valid, point.X, point.Z, transform.Rotation.Y));
        }
    }
}
