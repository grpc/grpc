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
        const string IsPeerAuthenticatedMetadataKey = "test_only_is_peer_authenticated";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        string rootCert;
        KeyCertificatePair keyCertPair;

        public void InitClientAndServer(bool clientAddKeyCertPair,
                SslClientCertificateRequestType clientCertRequestType,
                VerifyPeerCallback verifyPeerCallback = null)
        {
            rootCert = File.ReadAllText(TestCredentials.ClientCertAuthorityPath);
            keyCertPair = new KeyCertificatePair(
                File.ReadAllText(TestCredentials.ServerCertChainPath),
                File.ReadAllText(TestCredentials.ServerPrivateKeyPath));

            var serverCredentials = new SslServerCredentials(new[] { keyCertPair }, rootCert, clientCertRequestType);
            var clientCredentials = new SslCredentials(rootCert, clientAddKeyCertPair ? keyCertPair : null, verifyPeerCallback);

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
            if (channel != null)
            {
                channel.ShutdownAsync().Wait();
            }
            if (server != null)
            {
                server.ShutdownAsync().Wait();
            }
        }

        [Test]
        public async Task NoClientCert_DontRequestClientCertificate_Accepted()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.DontRequest);

            await CheckAccepted(expectPeerAuthenticated: false);
        }

        [Test]
        public async Task ClientWithCert_DontRequestClientCertificate_AcceptedButPeerNotAuthenticated()
        {
            InitClientAndServer(
                clientAddKeyCertPair: true,
                clientCertRequestType: SslClientCertificateRequestType.DontRequest);

            await CheckAccepted(expectPeerAuthenticated: false);
        }

        [Test]
        public async Task NoClientCert_RequestClientCertificateButDontVerify_Accepted()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.RequestButDontVerify);

            await CheckAccepted(expectPeerAuthenticated: false);
        }

        [Test]
        public async Task NoClientCert_RequestClientCertificateAndVerify_Accepted()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.RequestAndVerify);

            await CheckAccepted(expectPeerAuthenticated: false);
        }

        [Test]
        public async Task ClientWithCert_RequestAndRequireClientCertificateButDontVerify_Accepted()
        {
            InitClientAndServer(
                clientAddKeyCertPair: true,
                clientCertRequestType: SslClientCertificateRequestType.RequestAndRequireButDontVerify);

            await CheckAccepted(expectPeerAuthenticated: true);
            await CheckAuthContextIsPopulated();
        }

        [Test]
        public async Task ClientWithCert_RequestAndRequireClientCertificateAndVerify_Accepted()
        {
            InitClientAndServer(
                clientAddKeyCertPair: true,
                clientCertRequestType: SslClientCertificateRequestType.RequestAndRequireAndVerify);

            await CheckAccepted(expectPeerAuthenticated: true);
            await CheckAuthContextIsPopulated();
        }

        [Test]
        public void NoClientCert_RequestAndRequireClientCertificateButDontVerify_Rejected()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.RequestAndRequireButDontVerify);

            CheckRejected();
        }

        [Test]
        public void NoClientCert_RequestAndRequireClientCertificateAndVerify_Rejected()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.RequestAndRequireAndVerify);

            CheckRejected();
        }

        [Test]
        public void Constructor_LegacyForceClientAuth()
        {
            var creds = new SslServerCredentials(new[] { keyCertPair }, rootCert, true);
            Assert.AreEqual(SslClientCertificateRequestType.RequestAndRequireAndVerify, creds.ClientCertificateRequest);

            var creds2 = new SslServerCredentials(new[] { keyCertPair }, rootCert, false);
            Assert.AreEqual(SslClientCertificateRequestType.DontRequest, creds2.ClientCertificateRequest);
        }

        [Test]
        public void Constructor_NullRootCerts()
        {
            var keyCertPairs = new[] { keyCertPair };
            Assert.DoesNotThrow(() => new SslServerCredentials(keyCertPairs, null, SslClientCertificateRequestType.DontRequest));
            Assert.DoesNotThrow(() => new SslServerCredentials(keyCertPairs, null, SslClientCertificateRequestType.RequestAndVerify));
            Assert.DoesNotThrow(() => new SslServerCredentials(keyCertPairs, null, SslClientCertificateRequestType.RequestAndRequireButDontVerify));
            Assert.Throws(typeof(ArgumentNullException), () => new SslServerCredentials(keyCertPairs, null, SslClientCertificateRequestType.RequestAndRequireAndVerify));
        }

        [Test]
        public async Task VerifyPeerCallback_Accepted()
        {
            string targetNameFromCallback = null;
            string peerPemFromCallback = null;
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.DontRequest,
                verifyPeerCallback: (ctx) =>
                {
                    targetNameFromCallback = ctx.TargetName;
                    peerPemFromCallback = ctx.PeerPem;
                    return true;
                });
            await CheckAccepted(expectPeerAuthenticated: false);
            Assert.AreEqual(TestCredentials.DefaultHostOverride, targetNameFromCallback);
            var expectedServerPem = File.ReadAllText(TestCredentials.ServerCertChainPath).Replace("\r", "");
            Assert.AreEqual(expectedServerPem, peerPemFromCallback);
        }

        [Test]
        public void VerifyPeerCallback_CallbackThrows_Rejected()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.DontRequest,
                verifyPeerCallback: (ctx) =>
                {
                    throw new Exception("VerifyPeerCallback has thrown on purpose.");
                });
            CheckRejected();
        }

        [Test]
        public void VerifyPeerCallback_Rejected()
        {
            InitClientAndServer(
                clientAddKeyCertPair: false,
                clientCertRequestType: SslClientCertificateRequestType.DontRequest,
                verifyPeerCallback: (ctx) =>
                {
                    return false;
                });
            CheckRejected();
        }

        private async Task CheckAccepted(bool expectPeerAuthenticated)
        {
            var call = client.UnaryCallAsync(new SimpleRequest { ResponseSize = 10 });
            var response = await call;
            Assert.AreEqual(10, response.Payload.Body.Length);
            Assert.AreEqual(expectPeerAuthenticated.ToString(), call.GetTrailers().First((entry) => entry.Key == IsPeerAuthenticatedMetadataKey).Value);
        }

        private void CheckRejected()
        {
            var ex = Assert.Throws<RpcException>(() => client.UnaryCall(new SimpleRequest { ResponseSize = 10 }));
        if (ex.Status.StatusCode != StatusCode.Unavailable & ex.Status.StatusCode != StatusCode.Unknown) {
                Assert.Fail("Expect status to be either Unavailable or Unknown");
            }
	}

        private async Task CheckAuthContextIsPopulated()
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
                context.ResponseTrailers.Add(IsPeerAuthenticatedMetadataKey, context.AuthContext.IsPeerAuthenticated.ToString());
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
