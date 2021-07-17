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

using System.Runtime.InteropServices;
using BenchmarkDotNet.Attributes;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    public class PInvokeByteArrayBenchmark : CommonThreadedBase
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        protected override bool NeedsEnvironment => false;


        [Params(0)]
        public int PayloadSize { get; set; }

        const int Iterations = 5 * 1000 * 1000;  // High number to make the overhead of RunConcurrent negligible.
        [Benchmark(OperationsPerInvoke = Iterations)]
        public void AllocFree()
        {
            RunConcurrent(RunBody);
        }

        private void RunBody()
        {
            var payload = new byte[PayloadSize];
            for (int i = 0; i < Iterations; i++)
            {
                var gcHandle = GCHandle.Alloc(payload, GCHandleType.Pinned);
                var payloadPtr = gcHandle.AddrOfPinnedObject();
                Native.grpcsharp_test_nop(payloadPtr);
                gcHandle.Free();
            }
        }
    }
}
