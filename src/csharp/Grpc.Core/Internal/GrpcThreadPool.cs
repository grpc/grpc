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
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Pool of threads polling on the same completion queue.
    /// </summary>
    internal class GrpcThreadPool
    {
        readonly GrpcEnvironment environment;
        readonly object myLock = new object();
        readonly List<Thread> threads = new List<Thread>();
        readonly int poolSize;

        CompletionQueueSafeHandle cq;

        public GrpcThreadPool(GrpcEnvironment environment, int poolSize)
        {
            this.environment = environment;
            this.poolSize = poolSize;
        }

        public void Start()
        {
            lock (myLock)
            {
                if (cq != null)
                {
                    throw new InvalidOperationException("Already started.");
                }

                cq = CompletionQueueSafeHandle.Create();

                for (int i = 0; i < poolSize; i++)
                {
                    threads.Add(CreateAndStartThread(i));
                }
            }
        }

        public void Stop()
        {
            lock (myLock)
            {
                cq.Shutdown();

                Console.WriteLine("Waiting for GRPC threads to finish.");
                foreach (var thread in threads)
                {
                    thread.Join();
                }

                cq.Dispose();
            }
        }

        internal CompletionQueueSafeHandle CompletionQueue
        {
            get
            {
                return cq;
            }
        }

        private Thread CreateAndStartThread(int i)
        {
            var thread = new Thread(new ThreadStart(RunHandlerLoop));
            thread.IsBackground = false;
            thread.Start();
            thread.Name = "grpc " + i;
            return thread;
        }

        /// <summary>
        /// Body of the polling thread.
        /// </summary>
        private void RunHandlerLoop()
        {
            CompletionQueueEvent ev;
            do
            {
                ev = cq.Next();
                if (ev.type == GRPCCompletionType.OpComplete)
                {
                    bool success = (ev.success != 0);
                    IntPtr tag = ev.tag;
                    try
                    {
                        var callback = environment.CompletionRegistry.Extract(tag);
                        callback(success);
                    }
                    catch (Exception e)
                    {
                        Console.WriteLine("Exception occured while invoking completion delegate: " + e);
                    }
                }
            }
            while (ev.type != GRPCCompletionType.Shutdown);
            Console.WriteLine("Completion queue has shutdown successfully, thread " + Thread.CurrentThread.Name + " exiting.");
        }
    }
}
