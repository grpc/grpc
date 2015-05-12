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
using System.Threading;
using System.Threading.Tasks;
using grpc.testing;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Runs interop tests in-process.
    /// </summary>
    public class InteropClientServerTest
    {
        string host = "localhost";
        Server server;
        Channel channel;
        TestService.ITestServiceClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            GrpcEnvironment.Initialize();

            server = new Server();
            server.AddServiceDefinition(TestService.BindService(new TestServiceImpl()));
            int port = server.AddListeningPort(host, Server.PickUnusedPort, TestCredentials.CreateTestServerCredentials());
            server.Start();

            var channelArgs = ChannelArgs.CreateBuilder()
                .AddString(ChannelArgs.SslTargetNameOverrideKey, TestCredentials.DefaultHostOverride).Build();

            channel = new Channel(host + ":" + port, TestCredentials.CreateTestClientCredentials(true), channelArgs);
            client = TestService.NewStub(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.Dispose();

            server.ShutdownAsync().Wait();
            GrpcEnvironment.Shutdown();
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
        public void ClientStreaming()
        {
            InteropClient.RunClientStreaming(client);
        }

        [Test]
        public void ServerStreaming()
        {
            InteropClient.RunServerStreaming(client);
        }

        [Test]
        public void PingPong()
        {
            InteropClient.RunPingPong(client);
        }

        [Test]
        public void EmptyStream()
        {
            InteropClient.RunEmptyStream(client);
        }

        [Test]
        public void CancelAfterBegin()
        {
            InteropClient.RunCancelAfterBegin(client);
        }

        [Test]
        public void CancelAfterFirstResponse()
        {
            InteropClient.RunCancelAfterFirstResponse(client);
        }
    }
}
