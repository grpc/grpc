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
using Grpc.Core.Internal;

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
        static volatile GrpcEnvironment instance;

        readonly GrpcThreadPool threadPool;
        readonly CompletionRegistry completionRegistry;
        bool isClosed;

        /// <summary>
        /// Makes sure GRPC environment is initialized. Subsequent invocations don't have any
        /// effect unless you call Shutdown first.
        /// Although normal use cases assume you will call this just once in your application's
        /// lifetime (and call Shutdown once you're done), for the sake of easier testing it's
        /// allowed to initialize the environment again after it has been successfully shutdown.
        /// </summary>
        public static void Initialize()
        {
            lock (staticLock)
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
            lock (staticLock)
            {
                if (instance != null)
                {
                    instance.Close();
                    instance = null;

                    CheckDebugStats();
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

        internal static CompletionRegistry CompletionRegistry
        {
            get
            {
                var inst = instance;
                if (inst == null)
                {
                    throw new InvalidOperationException("GRPC environment not initialized");
                }
                return inst.completionRegistry;
            }
        }

        /// <summary>
        /// Creates gRPC environment.
        /// </summary>
        private GrpcEnvironment()
        {
            GrpcLog.RedirectNativeLogs(Console.Error);
            grpcsharp_init();
            completionRegistry = new CompletionRegistry();
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

        private static void CheckDebugStats()
        {
            var remainingClientCalls = DebugStats.ActiveClientCalls.Count;
            if (remainingClientCalls != 0)
            {                
                DebugWarning(string.Format("Detected {0} client calls that weren't disposed properly.", remainingClientCalls));
            }
            var remainingServerCalls = DebugStats.ActiveServerCalls.Count;
            if (remainingServerCalls != 0)
            {
                DebugWarning(string.Format("Detected {0} server calls that weren't disposed properly.", remainingServerCalls));
            }
            var pendingBatchCompletions = DebugStats.PendingBatchCompletions.Count;
            if (pendingBatchCompletions != 0)
            {
                DebugWarning(string.Format("Detected {0} pending batch completions.", pendingBatchCompletions));
            }
        }

        private static void DebugWarning(string message)
        {
            throw new Exception("Shutdown check: " + message);
        }
    }
}
