#region Copyright notice and license

// Copyright 2020 The gRPC Authors
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
    /// Tests for service-config based retries.
    /// </summary>
    public class RetryTest
    {
        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            var channelOptions = new ChannelOption[]
            {
                new ChannelOption(
                    "grpc.service_config",
                    "{\"methodConfig\":[{\"name\":[{\"service\":\"" + MockServiceHelper.ServiceName + "\",\"method\":\"Unary\"}],\"retryPolicy\":{\"maxAttempts\":3,\"initialBackoff\":\"0.5s\",\"maxBackoff\":\"5s\",\"backoffMultiplier\":2.0,\"retryableStatusCodes\":[\"UNAVAILABLE\"]}}, {\"name\":[{\"service\":\"" + MockServiceHelper.ServiceName + "\",\"method\":\"ServerStreaming\"}],\"retryPolicy\":{\"maxAttempts\":3,\"initialBackoff\":\"0.5s\",\"maxBackoff\":\"5s\",\"backoffMultiplier\":2.0,\"retryableStatusCodes\":[\"UNAVAILABLE\"]}}]}")
            };
            helper = new MockServiceHelper(channelOptions: channelOptions);

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
        public void ServiceConfigRetryPolicy_UnaryCall()
        {
            var counter = new AtomicCounter();

            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var attempt = counter.Increment();
                if (attempt <= 2)
                {
                    throw new RpcException(new Status(StatusCode.Unavailable, $"Attempt {attempt} failed on purpose"));
                }
                return Task.FromResult("PASS");
            });

            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
        }

        [Test]
        public async Task ServiceConfigRetryPolicy_AsyncUnaryCall()
        {
            var counter = new AtomicCounter();

            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var attempt = counter.Increment();
                if (attempt <= 2)
                {
                    throw new RpcException(new Status(StatusCode.Unavailable, $"Attempt {attempt} failed on purpose"));
                }
                return Task.FromResult("PASS");
            });

            Assert.AreEqual("PASS", await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
        }

        [Test]
        public async Task ServiceConfigRetryPolicy_ServerStreaming()
        {
            var counter = new AtomicCounter();

            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                var attempt = counter.Increment();
                if (attempt <= 2)
                {
                    throw new RpcException(new Status(StatusCode.Unavailable, $"Attempt {attempt} failed on purpose"));
                }
                await responseStream.WriteAllAsync(request.Split(new []{' '}));
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "A B C");
            CollectionAssert.AreEqual(new string[] { "A", "B", "C" }, await call.ResponseStream.ToListAsync());
            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
        }
    }
}
