#region Copyright notice and license

// Copyright 2017 gRPC authors.
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
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ChannelConnectivityTest
    {
        const string Host = "127.0.0.1";

        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper(Host);
            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task Channel_WaitForStateChangedAsync()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult(request);
            });

            Assert.ThrowsAsync(typeof(TaskCanceledException), 
                async () => await channel.WaitForStateChangedAsync(channel.State, DateTime.UtcNow.AddMilliseconds(10)));

            var stateChangedTask = channel.WaitForStateChangedAsync(channel.State);

            await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc");

            await stateChangedTask;
            Assert.AreEqual(ChannelState.Ready, channel.State);
        }

        [Test]
        public async Task Channel_ConnectAsync()
        {
            await channel.ConnectAsync();
            Assert.AreEqual(ChannelState.Ready, channel.State);

            await channel.ConnectAsync(DateTime.UtcNow.AddMilliseconds(1000));
            Assert.AreEqual(ChannelState.Ready, channel.State);
        }
    }
}
