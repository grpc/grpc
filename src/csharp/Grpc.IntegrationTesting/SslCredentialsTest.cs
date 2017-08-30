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

        [OneTimeSetUp]
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

        [OneTimeTearDown]
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
            public override Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
            {
                return Task.FromResult(new SimpleResponse { Payload = CreateZerosPayload(request.ResponseSize) });
            }

            public override async Task<StreamingInputCallResponse> StreamingInputCall(IAsyncStreamReader<StreamingInputCallRequest> requestStream, ServerCallContext context)
            {
                var authContext = context.AuthContext;
                await requestStream.ForEachAsync(request => TaskUtils.CompletedTask);

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
