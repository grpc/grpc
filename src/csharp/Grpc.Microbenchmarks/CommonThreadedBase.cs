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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using BenchmarkDotNet.Attributes;
using Grpc.Core;

namespace Grpc.Microbenchmarks
{

    // common base-type for tests that need to run with some level of concurrency;
    // note there's nothing *special* about this type - it is just to save some
    // boilerplate

    [ClrJob, CoreJob] // test .NET Core and .NET Framework
    [MemoryDiagnoser] // allocations
    public abstract class CommonThreadedBase
    {
        protected virtual bool NeedsEnvironment => true;

        [Params(1, 2, 4, 6)]
        public int ThreadCount { get; set; }

        protected GrpcEnvironment Environment { get; private set; }

        private List<Thread> workers;

        private List<BlockingCollection<Action>> dispatchQueues;

        [GlobalSetup]
        public virtual void Setup()
        {
            dispatchQueues = new List<BlockingCollection<Action>>();
            workers = new List<Thread>();
            for (int i = 0; i < ThreadCount; i++)
            {
                var dispatchQueue = new BlockingCollection<Action>();
                var thread = new Thread(new ThreadStart(() => WorkerThreadBody(dispatchQueue)));
                thread.Name = string.Format("threaded benchmark worker {0}", i);
                thread.Start();
                workers.Add(thread);
                dispatchQueues.Add(dispatchQueue);
            }

            if (NeedsEnvironment) Environment = GrpcEnvironment.AddRef();
        }

        [GlobalCleanup]
        public virtual void Cleanup()
        {
            for (int i = 0; i < ThreadCount; i++)
            {
                dispatchQueues[i].Add(null);  // null action request termination of the worker thread.
                workers[i].Join();
            }

            if (Environment != null)
            {
                Environment = null;
                GrpcEnvironment.ReleaseAsync().Wait();
            }
        }

        /// <summary>
        /// Runs the operation in parallel (once on each worker thread).
        /// This method tries to incur as little
        /// overhead as possible, but there is some inherent overhead
        /// that is hard to avoid (thread hop etc.). Therefore it is strongly
        /// recommended that the benchmarked operation runs long enough to
        /// make this overhead negligible.
        /// </summary>
        protected void RunConcurrent(Action operation)
        {
            var workItemTasks = new Task[ThreadCount];
            for (int i = 0; i < ThreadCount; i++)
            {
                var tcs = new TaskCompletionSource<object>();
                var workItem = new Action(() =>
                {
                    try
                    {
                        operation();
                        tcs.SetResult(null);
                    }
                    catch (Exception e)
                    {
                        tcs.SetException(e);
                    }
                });
                workItemTasks[i] = tcs.Task;
                dispatchQueues[i].Add(workItem);
            }
            Task.WaitAll(workItemTasks);
        }

        private void WorkerThreadBody(BlockingCollection<Action> dispatchQueue)
        {
            while(true)
            {
                var workItem = dispatchQueue.Take();
                if (workItem == null)
                {
                    // stop the worker if null action was provided
                    break;
                }
                workItem();
            }
        }
    }
}
