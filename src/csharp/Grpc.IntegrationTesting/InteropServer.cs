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
    public class InteropServer
    {
        private class ServerOptions
        {
            [Option("port", Default = 8070)]
            public int Port { get; set; }

            // Deliberately using nullable bool type to allow --use_tls=true syntax (as opposed to --use_tls)
            [Option("use_tls", Default = false)]
            public bool? UseTls { get; set; }
        }

        ServerOptions options;

        private InteropServer(ServerOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
        {
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            var parserResult = Parser.Default.ParseArguments<ServerOptions>(args)
                .WithNotParsed(errors => Environment.Exit(1))
                .WithParsed(options =>
                {
                    var interopServer = new InteropServer(options);
                    interopServer.Run();
                });
        }

        private void Run()
        {
            var server = new Server
            {
                Services = { TestService.BindService(new TestServiceImpl()) }
            };

            string host = "0.0.0.0";
            int port = options.Port;
            if (options.UseTls.Value)
            {
                server.Ports.Add(host, port, TestCredentials.CreateSslServerCredentials());
            }
            else
            {
                server.Ports.Add(host, options.Port, ServerCredentials.Insecure);
            }
            Console.WriteLine("Running server on " + string.Format("{0}:{1}", host, port));
            server.Start();

            server.ShutdownTask.Wait();
        }
    }
}
