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
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

using CommandLine;
using CommandLine.Text;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class QpsWorker
    {
        private class ServerOptions
        {
            [Option("driver_port", Default = 0)]
            public int DriverPort { get; set; }
        }

        ServerOptions options;

        private QpsWorker(ServerOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
        {
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            var parserResult = Parser.Default.ParseArguments<ServerOptions>(args)
                .WithNotParsed((x) => Environment.Exit(1))
                .WithParsed(options =>
                {
                    var workerServer = new QpsWorker(options);
                    workerServer.RunAsync().Wait();
                });
        }

        private async Task RunAsync()
        {
            string host = "0.0.0.0";
            int port = options.DriverPort;

            var tcs = new TaskCompletionSource<object>();
            var workerServiceImpl = new WorkerServiceImpl(() => { Task.Run(() => tcs.SetResult(null)); });
                
            var server = new Server
            {
                Services = { WorkerService.BindService(workerServiceImpl) },
                Ports = { new ServerPort(host, options.DriverPort, ServerCredentials.Insecure )}
            };
            int boundPort = server.Ports.Single().BoundPort;
            GrpcEnvironment.Logger.Info("Running qps worker server on {0}:{1}", host, boundPort);
            server.Start();
            await tcs.Task;
            await server.ShutdownAsync();

            GrpcEnvironment.Logger.Info("GC collection counts (after shutdown): gen0 {0}, gen1 {1}, gen2 {2}",
                GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2));
        }
    }
}
