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
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class PInvokeTest
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        int counter;

        /// <summary>
        /// (~1.26us .NET Windows)
        /// </summary>
        [Test]
        public void CompletionQueueCreateSyncDestroyBenchmark()
        {
            GrpcEnvironment.AddRef();  // completion queue requires gRPC environment being initialized.

            BenchmarkUtil.RunBenchmark(
                10, 10,
                () =>
                {
                    CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.CreateSync();
                    cq.Dispose();
                });

            GrpcEnvironment.ReleaseAsync().Wait();
        }

        /// <summary>
        /// Approximate results:
        /// (~80ns Mono Linux)
        /// (~110ns .NET Windows)
        /// </summary>
        [Test]
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public void NativeCallbackBenchmark()
        {
            OpCompletionDelegate handler = Handler;

            counter = 0;
            BenchmarkUtil.RunBenchmark(
                1000000, 10000000,
                () =>
                {
                    Native.grpcsharp_test_callback(handler);
                });
            Assert.AreNotEqual(0, counter);
        }

        /// <summary>
        /// Creating a new native-to-managed callback has significant overhead
        /// compared to using an existing one. We need to be aware of this.
        /// (~50us on Mono Linux!!!)
        /// (~1.1us on .NET Windows)
        /// </summary>
        [Test]
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public void NewNativeCallbackBenchmark()
        {
            counter = 0;
            BenchmarkUtil.RunBenchmark(
                10000, 10000,
                () =>
                {
                    Native.grpcsharp_test_callback(new OpCompletionDelegate(Handler));
                });
            Assert.AreNotEqual(0, counter);
        }

        /// <summary>
        /// Tests overhead of a simple PInvoke call.
        /// (~46ns .NET Windows)
        /// </summary>
        [Test]
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public void NopPInvokeBenchmark()
        {
            BenchmarkUtil.RunBenchmark(
                1000000, 100000000,
                () =>
                {
                    Native.grpcsharp_test_nop(IntPtr.Zero);
                });
        }

        private void Handler(bool success)
        {
            counter++;
        }
    }
}
