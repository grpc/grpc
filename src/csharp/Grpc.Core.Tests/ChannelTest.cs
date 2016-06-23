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
    }
}
