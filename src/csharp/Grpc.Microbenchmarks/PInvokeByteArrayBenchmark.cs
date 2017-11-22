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
    public class PInvokeByteArrayBenchmark
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        public void Init()
        {
        }

        public void Cleanup()
        {
        }

        public void Run(int threadCount, int iterations, int payloadSize)
        {
            Console.WriteLine(string.Format("PInvokeByteArrayBenchmark: threads={0}, iterations={1}, payloadSize={2}", threadCount, iterations, payloadSize));
            var threadedBenchmark = new ThreadedBenchmark(threadCount, () => ThreadBody(iterations, payloadSize));
            threadedBenchmark.Run();
        }

        private void ThreadBody(int iterations, int payloadSize)
        {
            var payload = new byte[payloadSize];
         
            var stopwatch = Stopwatch.StartNew();
            for (int i = 0; i < iterations; i++)
            {
                var gcHandle = GCHandle.Alloc(payload, GCHandleType.Pinned);
                var payloadPtr = gcHandle.AddrOfPinnedObject();
                Native.grpcsharp_test_nop(payloadPtr);
                gcHandle.Free();
            }
            stopwatch.Stop();
            Console.WriteLine("Elapsed millis: " + stopwatch.ElapsedMilliseconds);
        }
    }
}
