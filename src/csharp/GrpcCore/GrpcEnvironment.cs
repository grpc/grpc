using System;
using Google.GRPC.Core.Internal;
using System.Runtime.InteropServices;

namespace Google.GRPC.Core
{
    /// <summary>
    /// Encapsulates initialization and shutdown of gRPC library.
    /// </summary>
    public class GrpcEnvironment
    {
        const int THREAD_POOL_SIZE = 1;

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_init();

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_shutdown();

        static object staticLock = new object();
        static volatile GrpcEnvironment instance;
       
        readonly GrpcThreadPool threadPool;
        bool isClosed;

        /// <summary>
        /// Makes sure GRPC environment is initialized. Subsequent invocations don't have any
        /// effect unless you call Shutdown first.
        /// Although normal use cases assume you will call this just once in your application's
        /// lifetime (and call Shutdown once you're done), for the sake of easier testing it's 
        /// allowed to initialize the environment again after it has been successfully shutdown.
        /// </summary>
        public static void Initialize() {
            lock(staticLock)
            {
                if (instance == null)
                {
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
            lock(staticLock)
            {
                if (instance != null)
                {
                    instance.Close();
                    instance = null;
                }
            }
        }

        internal static GrpcThreadPool ThreadPool
        {
            get
            {
                var inst = instance;
                if (inst == null)
                {
                    throw new InvalidOperationException("GRPC environment not initialized");
                }
                return inst.threadPool;
            }
        }

        /// <summary>
        /// Creates gRPC environment.
        /// </summary>
        private GrpcEnvironment()
        {
            grpcsharp_init();
            threadPool = new GrpcThreadPool(THREAD_POOL_SIZE);
            threadPool.Start();
            // TODO: use proper logging here
            Console.WriteLine("GRPC initialized.");
        }

        /// <summary>
        /// Shuts down this environment.
        /// </summary>
        private void Close()
        {
            if (isClosed)
            {
                throw new InvalidOperationException("Close has already been called");
            }
            threadPool.Stop();
            grpcsharp_shutdown();
            isClosed = true;

            // TODO: use proper logging here
            Console.WriteLine("GRPC shutdown.");
        }
    }
}

