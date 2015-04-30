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
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ClientServerTest
    {
        const string Host = "localhost";
        const string ServiceName = "/tests.Test";

        static readonly Method<string, string> UnaryEchoStringMethod = new Method<string, string>(
            MethodType.Unary,
            "/tests.Test/UnaryEchoString",
            Marshallers.StringMarshaller,
            Marshallers.StringMarshaller);

        static readonly ServerServiceDefinition ServiceDefinition = ServerServiceDefinition.CreateBuilder(ServiceName)
            .AddMethod(UnaryEchoStringMethod, HandleUnaryEchoString).Build();

        [TestFixtureSetUp]
        public void Init()
        {
            GrpcEnvironment.Initialize();
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void UnaryCall()
        {
            var server = new Server();
            server.AddServiceDefinition(ServiceDefinition);
            int port = server.AddListeningPort(Host + ":0");
            server.Start();

            using (Channel channel = new Channel(Host + ":" + port))
            {
                var call = new Call<string, string>(ServiceName, UnaryEchoStringMethod, channel, Metadata.Empty);
                Assert.AreEqual("ABC", Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken)));
            }

            server.ShutdownAsync().Wait();
        }

        [Test]
        public void CallOnDisposedChannel()
        {
            var server = new Server();
            server.AddServiceDefinition(ServiceDefinition);
            int port = server.AddListeningPort(Host + ":0");
            server.Start();

            Channel channel = new Channel(Host + ":" + port);
            channel.Dispose();

            var call = new Call<string, string>(ServiceName, UnaryEchoStringMethod, channel, Metadata.Empty);
            try
            {
              Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken));
              Assert.Fail();
            }
            catch (ObjectDisposedException e)
            {
            }

            server.ShutdownAsync().Wait();
        }

        [Test]
        public void UnaryCallPerformance()
        {
            var server = new Server();
            server.AddServiceDefinition(ServiceDefinition);
            int port = server.AddListeningPort(Host + ":0");
            server.Start();

            using (Channel channel = new Channel(Host + ":" + port))
            {
                var call = new Call<string, string>(ServiceName, UnaryEchoStringMethod, channel, Metadata.Empty);
                BenchmarkUtil.RunBenchmark(100, 100,
                                           () => { Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken)); });
            }

            server.ShutdownAsync().Wait();
        }

        [Test]
        public void UnknownMethodHandler()
        {
            var server = new Server();
            server.AddServiceDefinition(ServerServiceDefinition.CreateBuilder(ServiceName).Build());
            int port = server.AddListeningPort(Host + ":0");
            server.Start();

            using (Channel channel = new Channel(Host + ":" + port))
            {
                var call = new Call<string, string>(ServiceName, UnaryEchoStringMethod, channel, Metadata.Empty);
                try
                {
                    Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken));
                    Assert.Fail();
                }
                catch (RpcException e)
                {
                    Assert.AreEqual(StatusCode.Unimplemented, e.Status.StatusCode);
                }
            }

            server.ShutdownAsync().Wait();
        }

        /// <summary>
        /// Handler for unaryEchoString method.
        /// </summary>
        private static Task<string> HandleUnaryEchoString(string request)
        {
            return Task.FromResult(request);
        }
    }
}
