using System;
using System.Runtime.InteropServices;

namespace Pyjion
{
    public static class Test
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct LibArgs
        {
            public IntPtr Message;
            public int Number;
        }

        public static int Hello(IntPtr arg, int argLength)
        {
            if (argLength < Marshal.SizeOf(typeof(LibArgs)))
            {
                return Marshal.SizeOf(typeof(LibArgs));
            }

            LibArgs libArgs = Marshal.PtrToStructure<LibArgs>(arg);
            Console.WriteLine($"Hello from .NET, I am {nameof(Test)}");
            string message = Marshal.PtrToStringUTF8(libArgs.Message);
            Console.WriteLine($"You said : '{message}'");
            return 0;
        }

        public delegate int HelloDelegate(int x, int y);

        public static int Hello2(int x, int y)
        {
            return x * y;
        }
    }
}
