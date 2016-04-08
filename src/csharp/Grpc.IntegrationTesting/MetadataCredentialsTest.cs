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
    public class MetadataCredentialsTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;
        List<ChannelOption> options;
        Mock<TestService.TestServiceBase> serviceMock;
        AsyncAuthInterceptor asyncAuthInterceptor;

        [SetUp]
        public void Init()
        {
            serviceMock = new Mock<TestService.TestServiceBase>();
            serviceMock.Setup(m => m.UnaryCall(It.IsAny<SimpleRequest>(), It.IsAny<ServerCallContext>()))
                .Returns(new Func<SimpleRequest, ServerCallContext, Task<SimpleResponse>>(UnaryCallHandler));

            server = new Server
            {
                Services = { TestService.BindService(serviceMock.Object) },
                Ports = { { Host, ServerPort.PickUnused, TestCredentials.CreateSslServerCredentials() } }
            };
            server.Start();

            options = new List<ChannelOption>
            {
                new ChannelOption(ChannelOptions.SslTargetNameOverride, TestCredentials.DefaultHostOverride)
            };

            asyncAuthInterceptor = new AsyncAuthInterceptor(async (context, metadata) =>
            {
                await Task.Delay(100).ConfigureAwait(false);  // make sure the operation is asynchronous.
                metadata.Add("authorization", "SECRET_TOKEN");
            });
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void MetadataCredentials()
        {
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(),
                CallCredentials.FromInterceptor(asyncAuthInterceptor));
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = TestService.NewClient(channel);

            client.UnaryCall(new SimpleRequest {});
        }

        [Test]
        public void MetadataCredentials_PerCall()
        {
            channel = new Channel(Host, server.Ports.Single().BoundPort, TestCredentials.CreateSslCredentials(), options);
            client = TestService.NewClient(channel);

            var callCredentials = CallCredentials.FromInterceptor(asyncAuthInterceptor);
            client.UnaryCall(new SimpleRequest { }, new CallOptions(credentials: callCredentials));
        }

        private Task<SimpleResponse> UnaryCallHandler(SimpleRequest request, ServerCallContext context)
        {
            var authToken = context.RequestHeaders.First((entry) => entry.Key == "authorization").Value;
            Assert.AreEqual("SECRET_TOKEN", authToken);
            return Task.FromResult(new SimpleResponse());
        }
    }
}
