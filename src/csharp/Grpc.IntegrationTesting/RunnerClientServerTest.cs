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
        const string Host = "localhost";
        IServerRunner serverRunner;

        [TestFixtureSetUp]
        public void Init()
        {
            var serverConfig = new ServerConfig
            {
                ServerType = ServerType.ASYNC_SERVER,
                PayloadConfig = new PayloadConfig
                {
                    SimpleParams = new SimpleProtoParams
                    {
                        RespSize = 100
                    }
                }
            };
            serverRunner = ServerRunners.CreateStarted(serverConfig);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            serverRunner.StopAsync().Wait();
        }

        [Test]
        public async Task ClientServerRunner()
        {
            var config = new ClientConfig
            {
                ServerTargets = { string.Format("{0}:{1}", Host, serverRunner.BoundPort) },
                RpcType = RpcType.UNARY,
                LoadParams = new LoadParams { ClosedLoop = new ClosedLoopParams() },
                PayloadConfig = new PayloadConfig
                {
                    SimpleParams = new SimpleProtoParams
                    {
                        ReqSize = 100
                    }
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
            System.Console.WriteLine("avg micros/call " + (long) ((stats.Latencies.Sum / stats.Latencies.Count) * 1000000));
        }
    }
}
