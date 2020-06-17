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

namespace Grpc.Core.Tests
{
    public class ChannelTest
    {
        [Test]
        public void Constructor_RejectsInvalidParams()
        {
            Assert.Throws(typeof(ArgumentNullException), () => new Channel(null, ChannelCredentials.Insecure));
        }

        [Test]
        public void Constructor_RejectsDuplicateOptions()
        {
            var options = new ChannelOption[]
            {
                new ChannelOption(ChannelOptions.PrimaryUserAgentString, "ABC"),
                new ChannelOption(ChannelOptions.PrimaryUserAgentString, "XYZ")
            };
            Assert.Throws(typeof(ArgumentException), () => new Channel("127.0.0.1", ChannelCredentials.Insecure, options));
        }

        [Test]
        public void State_IdleAfterCreation()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            Assert.AreEqual(ChannelState.Idle, channel.State);
            channel.ShutdownAsync().Wait();
        }

        [Test]
        public void WaitForStateChangedAsync_InvalidArgument()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            Assert.ThrowsAsync(typeof(ArgumentException), async () => await channel.WaitForStateChangedAsync(ChannelState.Shutdown));
            channel.ShutdownAsync().Wait();
        }

        [Test]
        public void ResolvedTarget()
        {
            var channel = new Channel("127.0.0.1", ChannelCredentials.Insecure);
            Assert.IsTrue(channel.ResolvedTarget.Contains("127.0.0.1"));
            channel.ShutdownAsync().Wait();
        }

        [Test]
        public void Shutdown_AllowedOnlyOnce()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            channel.ShutdownAsync().Wait();
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await channel.ShutdownAsync());
        }

        [Test]
        public async Task ShutdownTokenCancelledAfterShutdown()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            Assert.IsFalse(channel.ShutdownToken.IsCancellationRequested);
            var shutdownTask = channel.ShutdownAsync();
            Assert.IsTrue(channel.ShutdownToken.IsCancellationRequested);
            await shutdownTask;
        }

        [Test]
        public async Task StateIsShutdownAfterShutdown()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            await channel.ShutdownAsync();
            Assert.AreEqual(ChannelState.Shutdown, channel.State);
        }

        [Test]
        public async Task ShutdownFinishesWaitForStateChangedAsync()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            var stateChangedTask = channel.WaitForStateChangedAsync(ChannelState.Idle);
            var shutdownTask = channel.ShutdownAsync();
            await stateChangedTask;
            await shutdownTask;
        }

        [Test]
        public async Task OperationsThrowAfterShutdown()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            await channel.ShutdownAsync();
            Assert.ThrowsAsync(typeof(ObjectDisposedException), async () => await channel.WaitForStateChangedAsync(ChannelState.Idle));
            Assert.Throws(typeof(ObjectDisposedException), () => { var x = channel.ResolvedTarget; });
            Assert.ThrowsAsync(typeof(TaskCanceledException), async () => await channel.ConnectAsync());
        }

        [Test]
        public async Task ChannelBaseShutdownAsyncInvokesShutdownAsync()
        {
            var channel = new Channel("localhost", ChannelCredentials.Insecure);
            ChannelBase channelBase = channel;
            await channelBase.ShutdownAsync();
            // check that Channel.ShutdownAsync has run
            Assert.AreEqual(ChannelState.Shutdown, channel.State);
        }
    }
}
