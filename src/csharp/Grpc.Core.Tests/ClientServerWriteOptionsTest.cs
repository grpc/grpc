#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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
    public class ClientServerWriteOptionsTest
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
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task ServerStreamingCall_ServerUsesBufferHint()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                // if commented out, the first call to WriteAsync() will hang as it tries to cork the message,
                // but also needs to send the response headers uncorked (the exact reason is TBD).
                // if the response headers are sent separately in advance, things work fine.
                await context.WriteResponseHeadersAsync(new Metadata());

                responseStream.WriteOptions = new WriteOptions(WriteFlags.BufferHint);
                foreach (var response in request.Split(new []{' '}))
                {
                    await responseStream.WriteAsync(response);
                }
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "A B C");
            CollectionAssert.AreEqual(new string[] { "A", "B", "C" }, await call.ResponseStream.ToListAsync());

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
        }
    }
}
