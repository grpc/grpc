#region Copyright notice and license

// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#endregion

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Logging;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Pool of threads polling on a set of completions queues.
    /// </summary>
    internal class GrpcThreadPool
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<GrpcThreadPool>();
        const int FinishContinuationsSleepMillis = 10;
        const int MaxFinishContinuationsSleepTotalMillis = 10000;

        readonly GrpcEnvironment environment;
        readonly object myLock = new object();
        readonly List<Thread> threads = new List<Thread>();
        readonly int poolSize;
        readonly int completionQueueCount;
        readonly bool inlineHandlers;
        readonly WaitCallback runCompletionQueueEventCallbackSuccess;
        readonly WaitCallback runCompletionQueueEventCallbackFailure;
        readonly AtomicCounter queuedContinuationCounter = new AtomicCounter();

        readonly List<BasicProfiler> threadProfilers = new List<BasicProfiler>();  // profilers assigned to threadpool threads

        bool stopRequested;

        IReadOnlyCollection<CompletionQueueSafeHandle> completionQueues;

        /// <summary>
        /// Creates a thread pool threads polling on a set of completions queues.
        /// </summary>
        /// <param name="environment">Environment.</param>
        /// <param name="poolSize">Pool size.</param>
        /// <param name="completionQueueCount">Completion queue count.</param>
        /// <param name="inlineHandlers">Handler inlining.</param>
        public GrpcThreadPool(GrpcEnvironment environment, int poolSize, int completionQueueCount, bool inlineHandlers)
        {
            this.environment = environment;
            this.poolSize = poolSize;
            this.completionQueueCount = completionQueueCount;
            this.inlineHandlers = inlineHandlers;
            GrpcPreconditions.CheckArgument(poolSize >= completionQueueCount,
                "Thread pool size cannot be smaller than the number of completion queues used.");

            this.runCompletionQueueEventCallbackSuccess = new WaitCallback((callback) => RunCompletionQueueEventCallback((IOpCompletionCallback) callback, true));
            this.runCompletionQueueEventCallbackFailure = new WaitCallback((callback) => RunCompletionQueueEventCallback((IOpCompletionCallback) callback, false));
        }

        public void Start()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(completionQueues == null, "Already started.");
                completionQueues = CreateCompletionQueueList(environment, completionQueueCount);

                for (int i = 0; i < poolSize; i++)
                {
                    var optionalProfiler = i < threadProfilers.Count ? threadProfilers[i] : null;
                    threads.Add(CreateAndStartThread(i, optionalProfiler));
                }
            }
        }

        public Task StopAsync()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!stopRequested, "Stop already requested.");
                stopRequested = true;

                foreach (var cq in completionQueues)
                {
                    cq.Shutdown();
                }
            }

            return Task.Run(() =>
            {
                foreach (var thread in threads)
                {
                    thread.Join();
                }

                foreach (var cq in completionQueues)
                {
                    cq.Dispose();
                }

                for (int i = 0; i < threadProfilers.Count; i++)
                {
                    threadProfilers[i].Dump(string.Format("grpc_trace_thread_{0}.txt", i));
                }
            });
        }

        /// <summary>
        /// Returns true if there is at least one thread pool thread that hasn't
        /// already stopped.
        /// Threads can either stop because all completion queues shut down or
        /// because all foreground threads have already shutdown and process is
        /// going to exit.
        /// </summary>
        internal bool IsAlive
        {
            get
            {
                return threads.Any(t => t.ThreadState != ThreadState.Stopped);
            }
        }

        internal IReadOnlyCollection<CompletionQueueSafeHandle> CompletionQueues
        {
            get
            {
                return completionQueues;
            }
        }

        private Thread CreateAndStartThread(int threadIndex, IProfiler optionalProfiler)
        {
            var cqIndex = threadIndex % completionQueues.Count;
            var cq = completionQueues.ElementAt(cqIndex);

            var thread = new Thread(new ThreadStart(() => RunHandlerLoop(cq, optionalProfiler)));
            thread.IsBackground = true;
            thread.Name = string.Format("grpc {0} (cq {1})", threadIndex, cqIndex);
            thread.Start();

            return thread;
        }

        /// <summary>
        /// Body of the polling thread.
        /// </summary>
        private void RunHandlerLoop(CompletionQueueSafeHandle cq, IProfiler optionalProfiler)
        {
            if (optionalProfiler != null)
            {
                Profilers.SetForCurrentThread(optionalProfiler);
            }

            CompletionQueueEvent ev;
            do
            {
                ev = cq.Next();
                if (ev.type == CompletionQueueEvent.CompletionType.OpComplete)
                {
                    bool success = (ev.success != 0);
                    IntPtr tag = ev.tag;
                    try
                    {
                        var callback = cq.CompletionRegistry.Extract(tag);
                        queuedContinuationCounter.Increment();
                        if (!inlineHandlers)
                        {
                            // Use cached delegates to avoid unnecessary allocations
                            ThreadPool.QueueUserWorkItem(success ? runCompletionQueueEventCallbackSuccess : runCompletionQueueEventCallbackFailure, callback);
                        }
                        else
                        {
                            RunCompletionQueueEventCallback(callback, success);
                        }
                    }
                    catch (Exception e)
                    {
                        Logger.Error(e, "Exception occured while extracting event from completion registry.");
                    }
                }
            }
            while (ev.type != CompletionQueueEvent.CompletionType.Shutdown);

            // Continuations are running on default threadpool that consists of background threads.
            // GrpcThreadPool thread (a foreground thread) will not exit unless all queued work had
            // been finished to prevent terminating the continuations queued prematurely.
            int sleepIterations = 0;
            while (queuedContinuationCounter.Count != 0)
            {
                // Only happens on shutdown and having pending continuations shouldn't very common,
                // so sleeping here for a little bit is fine.
                if (sleepIterations >= MaxFinishContinuationsSleepTotalMillis / FinishContinuationsSleepMillis)
                {
                    Logger.Warning("Shutting down gRPC thread [{0}] with unfinished callbacks (Timed out waiting for callbacks to finish).",
                        Thread.CurrentThread.Name);
                    break;
                }
                Thread.Sleep(FinishContinuationsSleepMillis);
                sleepIterations ++;
            }
        }

        private static IReadOnlyCollection<CompletionQueueSafeHandle> CreateCompletionQueueList(GrpcEnvironment environment, int completionQueueCount)
        {
            var list = new List<CompletionQueueSafeHandle>();
            for (int i = 0; i < completionQueueCount; i++)
            {
                var completionRegistry = new CompletionRegistry(environment, () => environment.BatchContextPool.Lease(), () => environment.RequestCallContextPool.Lease());
                list.Add(CompletionQueueSafeHandle.CreateAsync(completionRegistry));
            }
            return list.AsReadOnly();
        }

        private void RunCompletionQueueEventCallback(IOpCompletionCallback callback, bool success)
        {
            try
            {
                callback.OnComplete(success);
            }
            catch (Exception e)
            {
                Logger.Error(e, "Exception occured while invoking completion delegate");
            }
            finally
            {
                queuedContinuationCounter.Decrement();
            }
        }
    }
}
