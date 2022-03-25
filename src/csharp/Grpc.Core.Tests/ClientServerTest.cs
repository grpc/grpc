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
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ClientServerTest
    {
        const string Host = "127.0.0.1";

        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper(Host);
            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();
        }

        [TearDown]
        public void Cleanup()
        {
            var task1 = channel.ShutdownAsync();
            Console.Error.WriteLine("shutting down client");
            task1.Wait();
            Console.Error.WriteLine("client shutdown finished");
            var task2 = server.ShutdownAsync();
            Console.Error.WriteLine("shutting down server");
            task2.Wait();
            Console.Error.WriteLine("server shutdown finished");
        }

        [Test]
        public async Task ServerStreamingCall_TrailersFromMultipleSourcesGetConcatenated()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>((request, responseStream, context) =>
            {
                // server streaming call hander simply ends the call by sending "InvalidArgument" status.
                throw new RpcException(new Status(StatusCode.InvalidArgument, ""));
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");

            // the invalid argument status code is received
            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.InvalidArgument, ex.Status.StatusCode);

            // sometimes, the empty response headers are nnot delivered to the client and the tests times out.
            var responseHeaders = await call.ResponseHeadersAsync;
            Console.Error.WriteLine("TEST DONE");
        }

        [Test]
        public void DuplexStreamingCall()
        {
            // DUMMY TEST ->  it only creates a client and a server via the Init() and Cleanup() methods.
            // under the hood, that does grpc_init() and grpc_shutdown(), which may or may not be important.

            // if this method is commented out, can no longer reproduce.
        }
    }
}
