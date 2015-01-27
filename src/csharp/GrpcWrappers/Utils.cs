using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
    public static class Utils
    {
        [DllImport("libgrpc.so")]
        static extern void grpc_init();

        [DllImport("libgrpc.so")]
        static extern void grpc_shutdown();

        /// <summary>
        /// Initializes GRPC C Core library.
        /// </summary>
        public static void Init()
        {
            grpc_init();
            Console.WriteLine("GRPC initialized.");
        }

        /// <summary>
        /// Shutdown GRPC C Core library.
        /// </summary>
        public static void Shutdown()
        {
            grpc_shutdown();
            Console.WriteLine("GRPC shutdown.");
        }

        public static void AssertCallOk(GRPCCallError callError)
        {
            Trace.Assert(callError == GRPCCallError.GRPC_CALL_OK, "Status not GRPC_CALL_OK");
        }

    }
}

