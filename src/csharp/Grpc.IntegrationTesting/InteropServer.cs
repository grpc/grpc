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
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Google.ProtocolBuffers;
using grpc.testing;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class InteropServer
    {
        private class ServerOptions
        {
            public bool help;
            public int? port = 8070;
            public bool useTls;
        }

        ServerOptions options;

        private InteropServer(ServerOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
        {
            Console.WriteLine("gRPC C# interop testing server");
            ServerOptions options = ParseArguments(args);

            if (!options.port.HasValue)
            {
                Console.WriteLine("Missing required argument.");
                Console.WriteLine();
                options.help = true;
            }

            if (options.help)
            {
                Console.WriteLine("Usage:");
                Console.WriteLine("  --port=PORT");
                Console.WriteLine("  --use_tls=BOOLEAN");
                Console.WriteLine();
                Environment.Exit(1);
            }

            var interopServer = new InteropServer(options);
            interopServer.Run();
        }

        private void Run()
        {
            var server = new Server();
            server.AddServiceDefinition(TestService.BindService(new TestServiceImpl()));

            string host = "0.0.0.0";
            int port = options.port.Value;
            if (options.useTls)
            {
                server.AddPort(host, port, TestCredentials.CreateTestServerCredentials());
            }
            else
            {
                server.AddPort(host, options.port.Value, ServerCredentials.Insecure);
            }
            Console.WriteLine("Running server on " + string.Format("{0}:{1}", host, port));
            server.Start();

            server.ShutdownTask.Wait();

            GrpcEnvironment.Shutdown();
        }

        private static ServerOptions ParseArguments(string[] args)
        {
            var options = new ServerOptions();
            foreach (string arg in args)
            {
                ParseArgument(arg, options);
                if (options.help)
                {
                    break;
                }
            }
            return options;
        }

        private static void ParseArgument(string arg, ServerOptions options)
        {
            Match match;
            match = Regex.Match(arg, "--port=(.*)");
            if (match.Success)
            {
                options.port = int.Parse(match.Groups[1].Value.Trim());
                return;
            }

            match = Regex.Match(arg, "--use_tls=(.*)");
            if (match.Success)
            {
                options.useTls = bool.Parse(match.Groups[1].Value.Trim());
                return;
            }

            Console.WriteLine(string.Format("Unrecognized argument \"{0}\"", arg));
            options.help = true;
        }
    }
}
