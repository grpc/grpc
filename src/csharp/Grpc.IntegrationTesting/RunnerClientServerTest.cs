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
    /// <summary>
    /// Runs performance tests in-process.
    /// </summary>
    public class RunnerClientServerTest
    {
        IServerRunner serverRunner;

        [TestFixtureSetUp]
        public void Init()
        {
            var serverConfig = new ServerConfig
            {
                ServerType = ServerType.AsyncServer
            };
            serverRunner = ServerRunners.CreateStarted(serverConfig);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            serverRunner.StopAsync().Wait();
        }


        [Test]
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public async Task ClientServerRunner()
        {
            var config = new ClientConfig
            {
                ServerTargets = { string.Format("{0}:{1}", "localhost", serverRunner.BoundPort) },
                RpcType = RpcType.Unary,
                LoadParams = new LoadParams { ClosedLoop = new ClosedLoopParams() },
                PayloadConfig = new PayloadConfig
                {
                    SimpleParams = new SimpleProtoParams
                    {
                        ReqSize = 100,
                        RespSize = 100
                    }
                },
                HistogramParams = new HistogramParams
                {
                    Resolution = 0.01,
                    MaxPossible = 60e9
                }
            };

            var runner = ClientRunners.CreateStarted(config);

            System.Console.WriteLine("Warming up");
            await Task.Delay(3000);
            runner.GetStats(true);  // throw away warm-up data

            System.Console.WriteLine("Benchmarking");
            await Task.Delay(3000);
            var stats = runner.GetStats(true);
            await runner.StopAsync();

            System.Console.WriteLine(stats);
            System.Console.WriteLine("avg micros/call " + (long) (stats.Latencies.Sum / stats.Latencies.Count / 1000.0));
        }
    }
}
