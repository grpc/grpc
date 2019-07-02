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

        [Params(1, 2, 4, 8, 12)]
        public int ThreadCount { get; set; }

        protected GrpcEnvironment Environment { get; private set; }

        [GlobalSetup]
        public virtual void Setup()
        {
            ThreadPool.GetMinThreads(out var workers, out var iocp);
            if (workers <= ThreadCount) ThreadPool.SetMinThreads(ThreadCount + 1, iocp);
            if (NeedsEnvironment) Environment = GrpcEnvironment.AddRef();
        }

        [GlobalCleanup]
        public virtual void Cleanup()
        {
            if (Environment != null)
            {
                Environment = null;
                GrpcEnvironment.ReleaseAsync().Wait();
            }
        }

        protected void RunConcurrent(Action operation)
        {
            Parallel.For(0, ThreadCount, _ => operation());
        }
    }
}
