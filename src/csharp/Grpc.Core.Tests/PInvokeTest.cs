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
        public void CompletionQueueCreateDestroyBenchmark()
        {
            GrpcEnvironment.AddRef();  // completion queue requires gRPC environment being initialized.

            BenchmarkUtil.RunBenchmark(
                10, 10,
                () =>
                {
                    CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.Create();
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
