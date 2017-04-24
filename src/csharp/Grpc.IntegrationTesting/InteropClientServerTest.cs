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
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Runs interop tests in-process.
    /// </summary>
    public class InteropClientServerTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new TestServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, TestCredentials.CreateSslServerCredentials() } }
            };
            server.Start();

            var options = new List<ChannelOption>
            {
                new ChannelOption(ChannelOptions.SslTargetNameOverride, TestCredentials.DefaultHostOverride)
            };
            int port = server.Ports.Single().BoundPort;
            channel = new Channel(Host, port, TestCredentials.CreateSslCredentials(), options);
            client = new TestService.TestServiceClient(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void EmptyUnary()
        {
            InteropClient.RunEmptyUnary(client);
        }

        [Test]
        public void LargeUnary()
        {
            InteropClient.RunLargeUnary(client);
        }

        [Test]
        public async Task ClientStreaming()
        {
            await InteropClient.RunClientStreamingAsync(client);
        }

        [Test]
        public async Task ServerStreaming()
        {
            await InteropClient.RunServerStreamingAsync(client);
        }

        [Test]
        public async Task PingPong()
        {
            await InteropClient.RunPingPongAsync(client);
        }

        [Test]
        public async Task EmptyStream()
        {
            await InteropClient.RunEmptyStreamAsync(client);
        }

        [Test]
        public async Task CancelAfterBegin()
        {
            await InteropClient.RunCancelAfterBeginAsync(client);
        }

        [Test]
        public async Task CancelAfterFirstResponse()
        {
            await InteropClient.RunCancelAfterFirstResponseAsync(client);
        }

        [Test]
        public async Task TimeoutOnSleepingServer()
        {
            await InteropClient.RunTimeoutOnSleepingServerAsync(client);
        }

        [Test]
        public async Task CustomMetadata()
        {
            await InteropClient.RunCustomMetadataAsync(client);
        }

        [Test]
        public async Task StatusCodeAndMessage()
        {
            await InteropClient.RunStatusCodeAndMessageAsync(client);
        }

        [Test]
        public void UnimplementedService()
        {
            InteropClient.RunUnimplementedService(new UnimplementedService.UnimplementedServiceClient(channel));
        }

        [Test]
        public void UnimplementedMethod()
        {
            InteropClient.RunUnimplementedMethod(client);
        }
    }
}
