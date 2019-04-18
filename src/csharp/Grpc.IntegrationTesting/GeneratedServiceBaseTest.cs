#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class GeneratedServiceBaseTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [SetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new UnimplementedTestServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, SslServerCredentials.Insecure } }
            };
            server.Start();
            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);
            client = new TestService.TestServiceClient(channel);
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void UnimplementedByDefault_Unary()
        {
            var ex = Assert.Throws<RpcException>(() => client.UnaryCall(new SimpleRequest { }));
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        [Test]
        public void UnimplementedByDefault_ClientStreaming()
        {
            var call = client.StreamingInputCall();

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call);
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        [Test]
        public void UnimplementedByDefault_ServerStreamingCall()
        {
            var call = client.StreamingOutputCall(new StreamingOutputCallRequest());

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        [Test]
        public void UnimplementedByDefault_DuplexStreamingCall()
        {
            var call = client.FullDuplexCall();

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        /// <summary>
        /// Implementation of TestService that doesn't override any methods.
        /// </summary>
        private class UnimplementedTestServiceImpl : TestService.TestServiceBase
        {
        }
    }
}
