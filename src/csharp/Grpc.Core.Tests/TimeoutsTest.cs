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
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    /// <summary>
    /// Tests for Deadline support.
    /// </summary>
    public class TimeoutsTest
    {
        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper();

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
        public void InfiniteDeadline()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                Assert.AreEqual(DateTime.MaxValue, context.Deadline);
                return "PASS";
            });

            // no deadline specified, check server sees infinite deadline
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));

            // DateTime.MaxValue deadline specified, check server sees infinite deadline
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(new CallOptions(deadline: DateTime.MaxValue)), "abc"));
        }

        [Test]
        public void DeadlineTransferredToServer()
        {
            var clientDeadline = DateTime.UtcNow + TimeSpan.FromDays(7);

            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                // A fairly relaxed check that the deadline set by client and deadline seen by server
                // are in agreement. C core takes care of the work with transferring deadline over the wire,
                // so we don't need an exact check here.
                Assert.IsTrue(Math.Abs((clientDeadline - context.Deadline).TotalMilliseconds) < 5000);
                return "PASS";
            });
            Calls.BlockingUnaryCall(helper.CreateUnaryCall(new CallOptions(deadline: clientDeadline)), "abc");
        }

        [Test]
        public void DeadlineInThePast()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                await Task.Delay(60000);
                return "FAIL";
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(new CallOptions(deadline: DateTime.MinValue)), "abc"));
            // We can't guarantee the status code always DeadlineExceeded. See issue #2685.
            Assert.Contains(ex.Status.StatusCode, new[] { StatusCode.DeadlineExceeded, StatusCode.Internal });
        }

        [Test]
        public void DeadlineExceededStatusOnTimeout()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                await Task.Delay(60000);
                return "FAIL";
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(new CallOptions(deadline: DateTime.UtcNow.Add(TimeSpan.FromSeconds(5)))), "abc"));
            // We can't guarantee the status code always DeadlineExceeded. See issue #2685.
            Assert.Contains(ex.Status.StatusCode, new[] { StatusCode.DeadlineExceeded, StatusCode.Internal });
        }

        [Test]
        public async Task ServerReceivesCancellationOnTimeout()
        {
            var serverReceivedCancellationTcs = new TaskCompletionSource<bool>();

            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) => 
            {
                // wait until cancellation token is fired.
                var tcs = new TaskCompletionSource<object>();
                context.CancellationToken.Register(() => { tcs.SetResult(null); });
                await tcs.Task;
                serverReceivedCancellationTcs.SetResult(true);
                return "";
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(new CallOptions(deadline: DateTime.UtcNow.Add(TimeSpan.FromSeconds(5)))), "abc"));
            // We can't guarantee the status code always DeadlineExceeded. See issue #2685.
            Assert.Contains(ex.Status.StatusCode, new[] { StatusCode.DeadlineExceeded, StatusCode.Internal });

            Assert.IsTrue(await serverReceivedCancellationTcs.Task);
        }
    }
}
