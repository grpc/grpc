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
using System.Linq;
using Grpc.Core;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class GrpcEnvironmentTest
    {
        [Test]
        public void InitializeAndShutdownGrpcEnvironment()
        {
            var env = GrpcEnvironment.AddRef();
            Assert.IsTrue(env.CompletionQueues.Count > 0);
            for (int i = 0; i < env.CompletionQueues.Count; i++)
            {
                Assert.IsNotNull(env.CompletionQueues.ElementAt(i));
            }
            GrpcEnvironment.ReleaseAsync().Wait();
        }

        [Test]
        public void SubsequentInvocations()
        {
            var env1 = GrpcEnvironment.AddRef();
            var env2 = GrpcEnvironment.AddRef();
            Assert.AreSame(env1, env2);
            GrpcEnvironment.ReleaseAsync().Wait();
            GrpcEnvironment.ReleaseAsync().Wait();
        }

        [Test]
        public void InitializeAfterShutdown()
        {
            Assert.AreEqual(0, GrpcEnvironment.GetRefCount());

            var env1 = GrpcEnvironment.AddRef();
            GrpcEnvironment.ReleaseAsync().Wait();

            var env2 = GrpcEnvironment.AddRef();
            GrpcEnvironment.ReleaseAsync().Wait();

            Assert.AreNotSame(env1, env2);
        }

        [Test]
        public void ReleaseWithoutAddRef()
        {
            Assert.AreEqual(0, GrpcEnvironment.GetRefCount());
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await GrpcEnvironment.ReleaseAsync());
        }

        [Test]
        public void GetCoreVersionString()
        {
            var coreVersion = GrpcEnvironment.GetCoreVersionString();
            var parts = coreVersion.Split('.');
            Assert.AreEqual(3, parts.Length);
        }
    }
}
