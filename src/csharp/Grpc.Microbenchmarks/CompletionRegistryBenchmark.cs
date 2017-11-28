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
using System.Runtime.InteropServices;
using System.Threading;
using Grpc.Core;
using Grpc.Core.Internal;
using System.Collections.Generic;
using System.Diagnostics;

namespace Grpc.Microbenchmarks
{
    public class CompletionRegistryBenchmark
    {
        GrpcEnvironment environment;

        public void Init()
        {
            environment = GrpcEnvironment.AddRef();
        }

        public void Cleanup()
        {
            GrpcEnvironment.ReleaseAsync().Wait();
        }

        public void Run(int threadCount, int iterations, bool useSharedRegistry)
        {
            Console.WriteLine(string.Format("CompletionRegistryBenchmark: threads={0}, iterations={1}, useSharedRegistry={2}", threadCount, iterations, useSharedRegistry));
            CompletionRegistry sharedRegistry = useSharedRegistry ? new CompletionRegistry(environment) : null;
            var threadedBenchmark = new ThreadedBenchmark(threadCount, () => ThreadBody(iterations, sharedRegistry));
            threadedBenchmark.Run();
            // TODO: parametrize by number of pending completions
        }

        private void ThreadBody(int iterations, CompletionRegistry optionalSharedRegistry)
        {
            var completionRegistry = optionalSharedRegistry ?? new CompletionRegistry(environment);
            var ctx = BatchContextSafeHandle.Create();
  
            var stopwatch = Stopwatch.StartNew();
            for (int i = 0; i < iterations; i++)
            {
                completionRegistry.Register(ctx.Handle, ctx);
                var callback = completionRegistry.Extract(ctx.Handle);
                // NOTE: we are not calling the callback to avoid disposing ctx.
            }
            stopwatch.Stop();
            Console.WriteLine("Elapsed millis: " + stopwatch.ElapsedMilliseconds);          

            ctx.Dispose();
        }

        private class NopCompletionCallback : IOpCompletionCallback
        {
            public void OnComplete(bool success)
            {

            }
        }
    }
}
