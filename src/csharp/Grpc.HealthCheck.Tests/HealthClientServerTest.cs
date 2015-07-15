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
using System.Text;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Health.V1Alpha;
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
        Grpc.Health.V1Alpha.Health.IHealthClient client;
        Grpc.HealthCheck.HealthServiceImpl serviceImpl;

        [TestFixtureSetUp]
        public void Init()
        {
            serviceImpl = new HealthServiceImpl();

            server = new Server();
            server.AddServiceDefinition(Grpc.Health.V1Alpha.Health.BindService(serviceImpl));
            int port = server.AddListeningPort(Host, Server.PickUnusedPort);
            server.Start();
            channel = new Channel(Host, port);

            client = Grpc.Health.V1Alpha.Health.NewStub(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.Dispose();

            server.ShutdownAsync().Wait();
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void ServiceIsRunning()
        {
            serviceImpl.SetStatus("", "", HealthCheckResponse.Types.ServingStatus.SERVING);

            var response = client.Check(HealthCheckRequest.CreateBuilder().SetHost("").SetService("").Build());
            Assert.AreEqual(HealthCheckResponse.Types.ServingStatus.SERVING, response.Status);
        }

        [Test]
        public void ServiceDoesntExist()
        {
            // TODO(jtattermusch): currently, this returns wrong status code, because we don't enable sending arbitrary status code from
            // server handlers yet.
            Assert.Throws(typeof(RpcException), () => client.Check(HealthCheckRequest.CreateBuilder().SetHost("").SetService("nonexistent.service").Build()));
        }

        // TODO(jtattermusch): add test with timeout once timeouts are supported
    }
}