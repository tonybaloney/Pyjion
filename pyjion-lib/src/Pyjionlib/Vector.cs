using System;
using System.Numerics;
using System.Runtime.InteropServices;

namespace Pyjionlib
{
    public static class PlaneOperations
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct ProductArgs
        {
            [MarshalAs(UnmanagedType.R8)]
            public float x1;
            [MarshalAs(UnmanagedType.R8)]
            public float y1;
            [MarshalAs(UnmanagedType.R8)]
            public float z1;
            [MarshalAs(UnmanagedType.R8)]
            public float d1;
            [MarshalAs(UnmanagedType.R8)]
            public float x2;
            [MarshalAs(UnmanagedType.R8)]
            public float y2;
            [MarshalAs(UnmanagedType.R8)]
            public float z2;
            [MarshalAs(UnmanagedType.R8)]
            public float d2;
        }

        public static int VectorDotProduct(IntPtr arg, int size)
        {
            ProductArgs args = Marshal.PtrToStructure<ProductArgs>(arg);
            var plane1 = new Plane(args.x1, args.y1, args.z1, args.d1);

            return (int)Plane.Dot(plane1, new Vector4(args.x2, args.y2, args.z2, args.d2));
        }


    }
}
