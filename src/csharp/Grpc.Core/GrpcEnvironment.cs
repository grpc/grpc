#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Encapsulates initialization and shutdown of gRPC library.
    /// </summary>
    public class GrpcEnvironment
    {
        const int THREAD_POOL_SIZE = 4;

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_init();

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_shutdown();

        static object staticLock = new object();
        static GrpcEnvironment instance;

        static ILogger logger = new ConsoleLogger();

        readonly GrpcThreadPool threadPool;
        readonly CompletionRegistry completionRegistry;
        readonly DebugStats debugStats = new DebugStats();
        bool isClosed;

        /// <summary>
        /// Returns an instance of initialized gRPC environment.
        /// Subsequent invocations return the same instance unless Shutdown has been called first.
        /// </summary>
        internal static GrpcEnvironment GetInstance()
        {
            lock (staticLock)
            {
                if (instance == null)
                {
                    instance = new GrpcEnvironment();
                }
                return instance;
            }
        }

        /// <summary>
        /// Shuts down the gRPC environment if it was initialized before.
        /// Blocks until the environment has been fully shutdown.
        /// </summary>
        public static void Shutdown()
        {
            lock (staticLock)
            {
                if (instance != null)
                {
                    instance.Close();
                    instance = null;
                }
            }
        }

        /// <summary>
        /// Gets application-wide logger used by gRPC.
        /// </summary>
        /// <value>The logger.</value>
        public static ILogger Logger
        {
            get
            {
                return logger;
            }
        }

        /// <summary>
        /// Sets the application-wide logger that should be used by gRPC.
        /// </summary>
        public static void SetLogger(ILogger customLogger)
        {
            Preconditions.CheckNotNull(customLogger);
            logger = customLogger;
        }

        /// <summary>
        /// Creates gRPC environment.
        /// </summary>
        private GrpcEnvironment()
        {
            NativeLogRedirector.Redirect();
            grpcsharp_init();
            completionRegistry = new CompletionRegistry(this);
            threadPool = new GrpcThreadPool(this, THREAD_POOL_SIZE);
            threadPool.Start();
            // TODO: use proper logging here
            Logger.Info("gRPC initialized.");
        }

        /// <summary>
        /// Gets the completion registry used by this gRPC environment.
        /// </summary>
        internal CompletionRegistry CompletionRegistry
        {
            get
            {
                return this.completionRegistry;
            }
        }

        /// <summary>
        /// Gets the completion queue used by this gRPC environment.
        /// </summary>
        internal CompletionQueueSafeHandle CompletionQueue
        {
            get
            {
                return this.threadPool.CompletionQueue;
            }
        }

        /// <summary>
        /// Gets the completion queue used by this gRPC environment.
        /// </summary>
        internal DebugStats DebugStats
        {
            get
            {
                return this.debugStats;
            }
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

            debugStats.CheckOK();

            Logger.Info("gRPC shutdown.");
        }

        /// <summary>
        /// Shuts down this environment asynchronously.
        /// </summary>
        private Task CloseAsync()
        {
            return Task.Run(() =>
            {
                try
                {
                    Close();
                }
                catch (Exception e)
                {
                    Logger.Error(e, "Error occured while shutting down GrpcEnvironment.");
                }
            });
        }
    }
}
