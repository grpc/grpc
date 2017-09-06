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
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                Assert.AreEqual(DateTime.MaxValue, context.Deadline);
                return Task.FromResult("PASS");
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

            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                // A fairly relaxed check that the deadline set by client and deadline seen by server
                // are in agreement. C core takes care of the work with transferring deadline over the wire,
                // so we don't need an exact check here.
                Assert.IsTrue(Math.Abs((clientDeadline - context.Deadline).TotalMilliseconds) < 5000);
                return Task.FromResult("PASS");
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
