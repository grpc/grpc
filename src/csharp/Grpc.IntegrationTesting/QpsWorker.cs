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
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

using CommandLine;
using CommandLine.Text;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class QpsWorker
    {
        private class ServerOptions
        {
            [Option("driver_port", DefaultValue = 0)]
            public int DriverPort { get; set; }

            [HelpOption]
            public string GetUsage()
            {
                var help = new HelpText
                {
                    Heading = "gRPC C# performance testing worker",
                    AddDashesToOption = true
                };
                help.AddPreOptionsLine("Usage:");
                help.AddOptions(this);
                return help;
            }
        }

        ServerOptions options;

        private QpsWorker(ServerOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
        {
            var options = new ServerOptions();
            if (!Parser.Default.ParseArguments(args, options))
            {
                Environment.Exit(1);
            }

            var workerServer = new QpsWorker(options);
            workerServer.Run();
        }

        private void Run()
        {
            string host = "0.0.0.0";
            int port = options.DriverPort;

            var server = new Server
            {
                Services = { WorkerService.BindService(new WorkerServiceImpl()) },
                Ports = { new ServerPort(host, options.DriverPort, ServerCredentials.Insecure )}
            };
            int boundPort = server.Ports.Single().BoundPort;
            Console.WriteLine("Running qps worker server on " + string.Format("{0}:{1}", host, boundPort));
            server.Start();

            server.ShutdownTask.Wait();
        }
    }
}
