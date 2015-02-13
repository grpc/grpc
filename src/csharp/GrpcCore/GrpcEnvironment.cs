#region Copyright notice and license

// Copyright 2014, Google Inc.
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

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_init();

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_shutdown();

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
            grpcsharp_init();
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
            grpcsharp_shutdown();

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

