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
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class CompletionQueueSafeHandleTest
    {
        [Test]
        public void CreateSyncAndDestroy()
        {
            GrpcEnvironment.AddRef();
            var cq = CompletionQueueSafeHandle.CreateSync();
            cq.Dispose();
            GrpcEnvironment.ReleaseAsync().Wait();
        }

        [Test]
        public void CreateAsyncAndShutdown()
        {
            var env = GrpcEnvironment.AddRef();
            var cq = CompletionQueueSafeHandle.CreateAsync(new CompletionRegistry(env));
            cq.Shutdown();
            var ev = cq.Next();
            cq.Dispose();
            GrpcEnvironment.ReleaseAsync().Wait();
            Assert.AreEqual(CompletionQueueEvent.CompletionType.Shutdown, ev.type);
            Assert.AreNotEqual(IntPtr.Zero, ev.success);
            Assert.AreEqual(IntPtr.Zero, ev.tag);
        }
    }
}
