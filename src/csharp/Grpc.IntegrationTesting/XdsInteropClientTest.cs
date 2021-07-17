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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class XdsInteropClientTest
    {
        const string Host = "localhost";

        BackendServiceImpl backendService;

        Server backendServer;
        Server lbStatsServer;
        Channel lbStatsChannel;
        LoadBalancerStatsService.LoadBalancerStatsServiceClient lbStatsClient;

        XdsInteropClient xdsInteropClient;

        [OneTimeSetUp]
        public void Init()
        {
            backendService = new BackendServiceImpl();

            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            backendServer = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(backendService) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            backendServer.Start();

            xdsInteropClient = new XdsInteropClient(new XdsInteropClient.ClientOptions
            {
                NumChannels = 1,
                Qps = 1,
                RpcTimeoutSec = 10,
                Rpc = "UnaryCall",
                Server = $"{Host}:{backendServer.Ports.Single().BoundPort}",
            });

            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            lbStatsServer = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { LoadBalancerStatsService.BindService(new LoadBalancerStatsServiceImpl(xdsInteropClient.StatsWatcher)) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            lbStatsServer.Start();

            int port = lbStatsServer.Ports.Single().BoundPort;
            lbStatsChannel = new Channel(Host, port, ChannelCredentials.Insecure);
            lbStatsClient = new LoadBalancerStatsService.LoadBalancerStatsServiceClient(lbStatsChannel);
        }

        [OneTimeTearDown]
        public void Cleanup()
        {
            lbStatsChannel.ShutdownAsync().Wait();
            lbStatsServer.ShutdownAsync().Wait();
            backendServer.ShutdownAsync().Wait();
        }

        [Test]
        public async Task SmokeTest()
        {
            string backendName = "backend1";
            backendService.UnaryHandler = (request, context) =>
            {
                return Task.FromResult(new SimpleResponse { Hostname = backendName });
            };

            var cancellationTokenSource = new CancellationTokenSource();
            var runChannelsTask = xdsInteropClient.RunChannelsAsync(cancellationTokenSource.Token);

            var stats = await lbStatsClient.GetClientStatsAsync(new LoadBalancerStatsRequest
            {
                NumRpcs = 5,
                TimeoutSec = 10,
            }, deadline: DateTime.UtcNow.AddSeconds(30));

            Assert.AreEqual(0, stats.NumFailures);
            Assert.AreEqual(backendName, stats.RpcsByPeer.Keys.Single());
            Assert.AreEqual(5, stats.RpcsByPeer[backendName]);
            Assert.AreEqual("UnaryCall", stats.RpcsByMethod.Keys.Single());
            Assert.AreEqual(backendName, stats.RpcsByMethod["UnaryCall"].RpcsByPeer_.Keys.Single());
            Assert.AreEqual(5, stats.RpcsByMethod["UnaryCall"].RpcsByPeer_[backendName]);

            await Task.Delay(100);

            var stats2 = await lbStatsClient.GetClientStatsAsync(new LoadBalancerStatsRequest
            {
                NumRpcs = 3,
                TimeoutSec = 10,
            }, deadline: DateTime.UtcNow.AddSeconds(30));

            Assert.AreEqual(0, stats2.NumFailures);
            Assert.AreEqual(backendName, stats2.RpcsByPeer.Keys.Single());
            Assert.AreEqual(3, stats2.RpcsByPeer[backendName]);
            Assert.AreEqual("UnaryCall", stats2.RpcsByMethod.Keys.Single());
            Assert.AreEqual(backendName, stats2.RpcsByMethod["UnaryCall"].RpcsByPeer_.Keys.Single());
            Assert.AreEqual(3, stats2.RpcsByMethod["UnaryCall"].RpcsByPeer_[backendName]);
            
            cancellationTokenSource.Cancel();
            await runChannelsTask;
        }

        [Test]
        public async Task HostnameReadFromResponseHeaders()
        {
            string correctBackendName = "backend1";
            backendService.UnaryHandler = async (request, context) =>
            {
                await context.WriteResponseHeadersAsync(new Metadata { {"hostname", correctBackendName} });
                return new SimpleResponse { Hostname = "wrong_hostname" };
            };

            var cancellationTokenSource = new CancellationTokenSource();
            var runChannelsTask = xdsInteropClient.RunChannelsAsync(cancellationTokenSource.Token);

            var stats = await lbStatsClient.GetClientStatsAsync(new LoadBalancerStatsRequest
            {
                NumRpcs = 3,
                TimeoutSec = 10,
            }, deadline: DateTime.UtcNow.AddSeconds(30));

            Assert.AreEqual(0, stats.NumFailures);
            Assert.AreEqual(correctBackendName, stats.RpcsByPeer.Keys.Single());
            Assert.AreEqual(3, stats.RpcsByPeer[correctBackendName]);
            
            cancellationTokenSource.Cancel();
            await runChannelsTask;
        }

        public class BackendServiceImpl : TestService.TestServiceBase
        {
            public UnaryServerMethod<SimpleRequest, SimpleResponse> UnaryHandler { get; set; }
            public UnaryServerMethod<Empty, Empty> EmptyHandler { get; set; }

            public override Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
            {
                return UnaryHandler(request, context);
            }

            public override Task<Empty> EmptyCall(Empty request, ServerCallContext context)
            {
                return EmptyHandler(request, context);
            }
        }
    }
}
