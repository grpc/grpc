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

        static readonly Method<string, string> EchoMethod = new Method<string, string>(
            MethodType.Unary,
            "/tests.Test/Echo",
            Marshallers.StringMarshaller,
            Marshallers.StringMarshaller);

        static readonly Method<string, string> ConcatAndEchoMethod = new Method<string, string>(
            MethodType.ClientStreaming,
            "/tests.Test/ConcatAndEcho",
            Marshallers.StringMarshaller,
            Marshallers.StringMarshaller);

        static readonly Method<string, string> NonexistentMethod = new Method<string, string>(
            MethodType.Unary,
            "/tests.Test/NonexistentMethod",
            Marshallers.StringMarshaller,
            Marshallers.StringMarshaller);

        static readonly ServerServiceDefinition ServiceDefinition = ServerServiceDefinition.CreateBuilder(ServiceName)
            .AddMethod(EchoMethod, EchoHandler)
            .AddMethod(ConcatAndEchoMethod, ConcatAndEchoHandler)
            .Build();

        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            server = new Server();
            server.AddServiceDefinition(ServiceDefinition);
            int port = server.AddListeningPort(Host, Server.PickUnusedPort);
            server.Start();
            channel = new Channel(Host, port);
        }

        [TearDown]
        public void Cleanup()
        {
            channel.Dispose();
            server.ShutdownAsync().Wait();
        }

        [TestFixtureTearDown]
        public void CleanupClass()
        {
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void UnaryCall()
        {
            var call = new Call<string, string>(ServiceName, EchoMethod, channel, Metadata.Empty);
            Assert.AreEqual("ABC", Calls.BlockingUnaryCall(call, "ABC", CancellationToken.None));
        }

        [Test]
        public void UnaryCall_ServerHandlerThrows()
        {
            var call = new Call<string, string>(ServiceName, EchoMethod, channel, Metadata.Empty);
            try
            {
                Calls.BlockingUnaryCall(call, "THROW", CancellationToken.None);
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode); 
            }
        }

        [Test]
        public void AsyncUnaryCall()
        {
            var call = new Call<string, string>(ServiceName, EchoMethod, channel, Metadata.Empty);
            var result = Calls.AsyncUnaryCall(call, "ABC", CancellationToken.None).Result;
            Assert.AreEqual("ABC", result);
        }

        [Test]
        public void AsyncUnaryCall_ServerHandlerThrows()
        {
            Task.Run(async () =>
            {
                var call = new Call<string, string>(ServiceName, EchoMethod, channel, Metadata.Empty);
                try
                {
                    await Calls.AsyncUnaryCall(call, "THROW", CancellationToken.None);
                    Assert.Fail();
                }
                catch (RpcException e)
                {
                    Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode);
                }
            }).Wait();
        }

        [Test]
        public void ClientStreamingCall()
        {
            Task.Run(async () => 
            {
                var call = new Call<string, string>(ServiceName, ConcatAndEchoMethod, channel, Metadata.Empty);
                var callResult = Calls.AsyncClientStreamingCall(call, CancellationToken.None);

                await callResult.RequestStream.WriteAll(new string[] { "A", "B", "C" });
                Assert.AreEqual("ABC", await callResult.Result);
            }).Wait();
        }

        [Test]
        public void ClientStreamingCall_CancelAfterBegin()
        {
            Task.Run(async () => 
            {
                var call = new Call<string, string>(ServiceName, ConcatAndEchoMethod, channel, Metadata.Empty);

                var cts = new CancellationTokenSource();
                var callResult = Calls.AsyncClientStreamingCall(call, cts.Token);

                // TODO(jtattermusch): we need this to ensure call has been initiated once we cancel it.
                await Task.Delay(1000);
                cts.Cancel();

                try
                {
                    await callResult.Result;
                }
                catch (RpcException e)
                {
                    Assert.AreEqual(StatusCode.Cancelled, e.Status.StatusCode); 
                }
            }).Wait();
        }

        [Test]
        public void UnaryCall_DisposedChannel()
        {
            channel.Dispose();

            var call = new Call<string, string>(ServiceName, EchoMethod, channel, Metadata.Empty);
            Assert.Throws(typeof(ObjectDisposedException), () => Calls.BlockingUnaryCall(call, "ABC", CancellationToken.None));
        }

        [Test]
        public void UnaryCallPerformance()
        {
            var call = new Call<string, string>(ServiceName, EchoMethod, channel, Metadata.Empty);
            BenchmarkUtil.RunBenchmark(100, 100,
                                       () => { Calls.BlockingUnaryCall(call, "ABC", default(CancellationToken)); });
        }
            
        [Test]
        public void UnknownMethodHandler()
        {
            var call = new Call<string, string>(ServiceName, NonexistentMethod, channel, Metadata.Empty);
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

        private static async Task<string> EchoHandler(ServerCallContext context, string request)
        {
            if (request == "THROW")
            {
                throw new Exception("This was thrown on purpose by a test");
            }
            return request;
        }

        private static async Task<string> ConcatAndEchoHandler(ServerCallContext context, IAsyncStreamReader<string> requestStream)
        {
            string result = "";
            await requestStream.ForEach(async (request) =>
            {
                if (request == "THROW")
                {
                    throw new Exception("This was thrown on purpose by a test");
                }
                result += request;
            });
            // simulate processing takes some time.
            await Task.Delay(250);
            return result;
        }
    }
}
