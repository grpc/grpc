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
    public class GrpcEnvironment
    {
        [DllImport("libgrpc.so")]
        static extern void grpc_init();

        [DllImport("libgrpc.so")]
        static extern void grpc_shutdown();

        private static object staticLock = new object();
        private static volatile GrpcEnvironment instance;

        private object myLock = new object();
        private bool shutdown = false;

        private GrpcEnvironment()
        {
            GrpcInit();
        }

        public static GrpcEnvironment Instance
        {
            get
            {
                if (instance == null)
                {
                    EnsureInitialized();
                }
                return instance;
            }
        }

        public static void EnsureInitialized() {
            lock(staticLock)
            {
                if (instance == null) {
                    instance = new GrpcEnvironment();
                }
            }
        }

        /// <summary>
        /// Shuts down the GRPC environment if it was initialized before.
        /// Repeated invocations have no effect.
        /// </summary>
        public static void Shutdown()
        {
            bool instanceExists;    
            lock(staticLock)
            {
                instanceExists = instance != null;
            }

            if (instanceExists)
            {
                Instance.ShutdownInstance();
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

        /// <summary>
        /// Shuts down the GRPC environment.
        /// Repeated invocations have no effect.
        /// </summary>
        private void ShutdownInstance()
        {
            bool invokeShutdown = false;

            lock (myLock)
            {
                invokeShutdown = !shutdown;
                shutdown = true;
            }

            GrpcShutdown();
        }
    }
}

