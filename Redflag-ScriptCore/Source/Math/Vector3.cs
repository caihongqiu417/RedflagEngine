using System;

namespace Redflag
{
    public struct Vector3
    {
        public float X, Y, Z;

        public static Vector3 Zero => new Vector3(0, 0, 0);

        public Vector3(float x, float y, float z)
        {
            X = x; Y = y; Z = z;
        }

        public float LengthSquared()
        {
            return X * X + Y * Y + Z * Z;
        }

        public float Length()
        {
            return (float)Math.Sqrt(LengthSquared());
        }

        public Vector3 Normalized()
        {
            float length = Length();
            if (length <= 0.0001f)
                return Zero;

            return this / length;
        }

        public static Vector3 operator +(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        public static Vector3 operator -(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        public static Vector3 operator *(Vector3 vector, float scalar)
        {
            return new Vector3(vector.X * scalar, vector.Y * scalar, vector.Z * scalar);
        }

        public static Vector3 operator /(Vector3 vector, float scalar)
        {
            return new Vector3(vector.X / scalar, vector.Y / scalar, vector.Z / scalar);
        }
    }
}
