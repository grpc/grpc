#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
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
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using Moq;
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
            server = new Server
            {
                Services = { TestService.BindService(new UnimplementedTestServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, SslServerCredentials.Insecure } }
            };
            server.Start();
            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);
            client = TestService.NewClient(channel);
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
        public async Task UnimplementedByDefault_ClientStreaming()
        {
            var call = client.StreamingInputCall();

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call);
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        [Test]
        public async Task UnimplementedByDefault_ServerStreamingCall()
        {
            var call = client.StreamingOutputCall(new StreamingOutputCallRequest());

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        [Test]
        public async Task UnimplementedByDefault_DuplexStreamingCall()
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
