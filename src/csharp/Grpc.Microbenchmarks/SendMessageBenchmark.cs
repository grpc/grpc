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
using Grpc.Core;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    public class SendMessageBenchmark : CommonThreadedBase
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        public override void Setup()
        {
            Native.grpcsharp_test_override_method("grpcsharp_call_start_batch", "nop");
            base.Setup();
        }

        [Params(0)]
        public int PayloadSize { get; set; }

        const int Iterations = 5 * 1000 * 1000;  // High number to make the overhead of RunConcurrent negligible.
        [Benchmark(OperationsPerInvoke = Iterations)]
        public void SendMessage()
        {
            RunConcurrent(RunBody);
        }

        private void RunBody()
        {
            var completionRegistry = new CompletionRegistry(Environment, () => Environment.BatchContextPool.Lease(), () => throw new NotImplementedException());
            var cq = CompletionQueueSafeHandle.CreateAsync(completionRegistry);
            var call = CreateFakeCall(cq);

            var sendCompletionCallback = new NopSendCompletionCallback();
            var sliceBuffer = SliceBufferSafeHandle.Create();
            var writeFlags = default(WriteFlags);

            for (int i = 0; i < Iterations; i++)
            {
                // SendMessage steals the slices from the slice buffer, so we need to repopulate in each iteration.
                sliceBuffer.Reset();
                sliceBuffer.GetSpan(PayloadSize);
                sliceBuffer.Advance(PayloadSize);

                call.StartSendMessage(sendCompletionCallback, sliceBuffer, writeFlags, false);
                var callback = completionRegistry.Extract(completionRegistry.LastRegisteredKey);
                callback.OnComplete(true);
            }
            sliceBuffer.Dispose();
            cq.Dispose();
        }

        private static CallSafeHandle CreateFakeCall(CompletionQueueSafeHandle cq)
        {
            var call = CallSafeHandle.CreateFake(new IntPtr(0xdead), cq);
            bool success = false;
            while (!success)
            {
                // avoid calling destroy on a nonexistent grpc_call pointer
                call.DangerousAddRef(ref success);
            }
            return call;
        }

        private class NopSendCompletionCallback : ISendCompletionCallback
        {
            public void OnSendCompletion(bool success)
            {
                // NOP
            }
        }
    }
}
