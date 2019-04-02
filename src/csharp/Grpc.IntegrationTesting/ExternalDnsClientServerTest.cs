#region Copyright notice and license

// Copyright 2019 The gRPC Authors.
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
    /// Runs interop tests in-process, with that client using a target
    /// name that using a target name that triggers interaction with
    /// external DNS servers (even though it resolves to the in-proc server).
    /// This test is a trimmed-down sibling test to the one in
    /// "ExternalDnsWithTracingClientServerTest", and is meant mostly for
    /// comparison with that one.
    /// </summary>
    public class ExternalDnsClientServerTest
    {
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [OneTimeSetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new TestServiceImpl()) },
                Ports = { { "[::1]", ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();

            int port = server.Ports.Single().BoundPort;
            channel = new Channel("loopback6.unittest.grpc.io", port, ChannelCredentials.Insecure);
            client = new TestService.TestServiceClient(channel);
        }

        [OneTimeTearDown]
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
    }
}
