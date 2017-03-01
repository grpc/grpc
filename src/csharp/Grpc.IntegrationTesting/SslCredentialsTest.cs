#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
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

            server = new Server
            {
                Services = { TestService.BindService(new TestServiceImpl()) },
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
    }
}
