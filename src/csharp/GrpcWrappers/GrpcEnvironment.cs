using System;
using Google.GRPC.Wrappers;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// Encapsulates initialization and shutdown of GRPC C core library.
    /// You should not need to initialize it manually, as static constructors
    /// should load the library when needed.
    /// </summary>
    public static class GrpcEnvironment
    {
        [DllImport("libgrpc.so")]
        static extern void grpc_init();

        [DllImport("libgrpc.so")]
        static extern void grpc_shutdown();

        static object staticLock = new object();
        static bool initCalled = false;
        static bool shutdownCalled = false;

        /// <summary>
        /// Makes sure GRPC environment is initialized.
        /// </summary>
        public static void EnsureInitialized() {
            lock(staticLock)
            {
                if (!initCalled)
                {
                    initCalled = true;
                    GrpcInit();                      
                }
            }
        }

        /// <summary>
        /// Shuts down the GRPC environment if it was initialized before.
        /// Repeated invocations have no effect.
        /// </summary>
        public static void Shutdown()
        {
            lock(staticLock)
            {
                if (initCalled && !shutdownCalled)
                {
                    shutdownCalled = true;
                    GrpcShutdown();
                }
            }
        }

        /// <summary>
        /// Initializes GRPC C Core library.
        /// </summary>
        private static void GrpcInit()
        {
            grpc_init();

            // TODO: use proper logging here
            Console.WriteLine("GRPC initialized.");
        }

        /// <summary>
        /// Shutdown GRPC C Core library.
        /// </summary>
        private static void GrpcShutdown()
        {
            grpc_shutdown();

            // TODO: use proper logging here
            Console.WriteLine("GRPC shutdown.");
        }
    }
}

