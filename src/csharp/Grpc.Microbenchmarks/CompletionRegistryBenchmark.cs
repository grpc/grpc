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
using BenchmarkDotNet.Attributes;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    public class CompletionRegistryBenchmark : CommonThreadedBase
    {
        [Params(false, true)]
        public bool UseSharedRegistry { get; set; }

        const int Iterations = 5 * 1000 * 1000;  // High number to make the overhead of RunConcurrent negligible.
        [Benchmark(OperationsPerInvoke = Iterations)]
        public void RegisterExtract()
        {
            CompletionRegistry sharedRegistry = UseSharedRegistry ? new CompletionRegistry(Environment, () => BatchContextSafeHandle.Create(), () => RequestCallContextSafeHandle.Create()) : null;
            RunConcurrent(() =>
            {
                RunBody(sharedRegistry);
            });
        }

        private void RunBody(CompletionRegistry optionalSharedRegistry)
        {
            var completionRegistry = optionalSharedRegistry ?? new CompletionRegistry(Environment, () => throw new NotImplementedException(), () => throw new NotImplementedException());
            var ctx = BatchContextSafeHandle.Create();

            for (int i = 0; i < Iterations; i++)
            {
                completionRegistry.Register(ctx.Handle, ctx);
                var callback = completionRegistry.Extract(ctx.Handle);
                // NOTE: we are not calling the callback to avoid disposing ctx.
            }
            ctx.Recycle();
        }
    }
}
