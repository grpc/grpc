#region Copyright notice and license
// Copyright 2015 gRPC authors.
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
using System.Text;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Health.V1;
using NUnit.Framework;

namespace Grpc.HealthCheck.Tests
{
    /// <summary>
    /// Health client talks to health server.
    /// </summary>
    public class HealthClientServerTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        Grpc.Health.V1.Health.HealthClient client;
        Grpc.HealthCheck.HealthServiceImpl serviceImpl;

        [OneTimeSetUp]
        public void Init()
        {
            serviceImpl = new HealthServiceImpl();

            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { Grpc.Health.V1.Health.BindService(serviceImpl) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();
            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);

            client = new Grpc.Health.V1.Health.HealthClient(channel);
        }

        [OneTimeTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();

            server.ShutdownAsync().Wait();
        }

        [Test]
        public void ServiceIsRunning()
        {
            serviceImpl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);

            var response = client.Check(new HealthCheckRequest { Service = "" });
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.Serving, response.Status);
        }

        [Test]
        public void ServiceDoesntExist()
        {
            var ex = Assert.Throws<RpcException>(() => client.Check(new HealthCheckRequest { Service = "nonexistent.service" }));
            Assert.AreEqual(StatusCode.NotFound, ex.Status.StatusCode);
        }

        // TODO(jtattermusch): add test with timeout once timeouts are supported
    }
}
