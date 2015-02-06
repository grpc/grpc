using System;
using Google.GRPC.Core.Internal;
using System.Runtime.InteropServices;

namespace Google.GRPC.Core
{
    /// <summary>
    /// Encapsulates initialization and shutdown of GRPC C core library.
    /// You should not need to initialize it manually, as static constructors
    /// should load the library when needed.
    /// </summary>
    public static class GrpcEnvironment
    {
        const int THREAD_POOL_SIZE = 1;

        [DllImport("libgrpc.so")]
        static extern void grpc_init();

        [DllImport("libgrpc.so")]
        static extern void grpc_shutdown();

        static object staticLock = new object();
        static bool initCalled = false;
        static bool shutdownCalled = false;

        static GrpcThreadPool threadPool = new GrpcThreadPool(THREAD_POOL_SIZE);

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
            threadPool.Start();
            // TODO: use proper logging here
            Console.WriteLine("GRPC initialized.");
        }

        /// <summary>
        /// Shutdown GRPC C Core library.
        /// </summary>
        private static void GrpcShutdown()
        {
            threadPool.Stop();
            grpc_shutdown();

            // TODO: use proper logging here
            Console.WriteLine("GRPC shutdown.");
        }

        internal static GrpcThreadPool ThreadPool
        {
            get
            {
                return threadPool;
            }
        }
    }
}

