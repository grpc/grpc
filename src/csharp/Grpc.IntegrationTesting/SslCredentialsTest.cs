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
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Test SSL credentials where server authenticates client 
    /// and client authenticates the server.
    /// </summary>
    public class SslCredentialsTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            var rootCert = File.ReadAllText(TestCredentials.ClientCertAuthorityPath);
            var keyCertPair = new KeyCertificatePair(
                File.ReadAllText(TestCredentials.ServerCertChainPath),
                File.ReadAllText(TestCredentials.ServerPrivateKeyPath));

            var serverCredentials = new SslServerCredentials(new[] { keyCertPair }, rootCert, true);
            var clientCredentials = new SslCredentials(rootCert, keyCertPair);

            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new SslCredentialsTestServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, serverCredentials } }
            };
            server.Start();

            var options = new List<ChannelOption>
            {
                new ChannelOption(ChannelOptions.SslTargetNameOverride, TestCredentials.DefaultHostOverride)
            };

            channel = new Channel(Host, server.Ports.Single().BoundPort, clientCredentials, options);
            client = new TestService.TestServiceClient(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void AuthenticatedClientAndServer()
        {
            var response = client.UnaryCall(new SimpleRequest { ResponseSize = 10 });
            Assert.AreEqual(10, response.Payload.Body.Length);
        }

        [Test]
        public async Task AuthContextIsPopulated()
        {
            var call = client.StreamingInputCall();
            await call.RequestStream.CompleteAsync();
            var response = await call.ResponseAsync;
            Assert.AreEqual(12345, response.AggregatedPayloadSize);
        }

        private class SslCredentialsTestServiceImpl : TestService.TestServiceBase
        {
            public override async Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
            {
                return new SimpleResponse { Payload = CreateZerosPayload(request.ResponseSize) };
            }

            public override async Task<StreamingInputCallResponse> StreamingInputCall(IAsyncStreamReader<StreamingInputCallRequest> requestStream, ServerCallContext context)
            {
                var authContext = context.AuthContext;
                await requestStream.ForEachAsync(async request => {});

                Assert.IsTrue(authContext.IsPeerAuthenticated);
                Assert.AreEqual("x509_subject_alternative_name", authContext.PeerIdentityPropertyName);
                Assert.IsTrue(authContext.PeerIdentity.Count() > 0);
                Assert.AreEqual("ssl", authContext.FindPropertiesByName("transport_security_type").First().Value);

                return new StreamingInputCallResponse { AggregatedPayloadSize = 12345 };
            }

            private static Payload CreateZerosPayload(int size)
            {
                return new Payload { Body = ByteString.CopyFrom(new byte[size]) };
            }
        }
    }
}
