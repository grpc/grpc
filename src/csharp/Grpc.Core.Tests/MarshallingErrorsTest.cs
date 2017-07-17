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
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class MarshallingErrorsTest
    {
        const string Host = "127.0.0.1";

        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            var marshaller = new Marshaller<string>(
                (str) =>
                {
                    if (str == "UNSERIALIZABLE_VALUE")
                    {
                        // Google.Protobuf throws exception inherited from IOException
                        throw new IOException("Error serializing the message.");
                    }
                    return System.Text.Encoding.UTF8.GetBytes(str); 
                },
                (payload) =>
                {
                    var s = System.Text.Encoding.UTF8.GetString(payload);
                    if (s == "UNPARSEABLE_VALUE")
                    {
                        // Google.Protobuf throws exception inherited from IOException
                        throw new IOException("Error parsing the message.");
                    }
                    return s;
                });
            helper = new MockServiceHelper(Host, marshaller);
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
        public void ResponseParsingError_UnaryResponse()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("UNPARSEABLE_VALUE");
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "REQUEST"));
            Assert.AreEqual(StatusCode.Internal, ex.Status.StatusCode);
        }

        [Test]
        public void ResponseParsingError_StreamingResponse()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                await responseStream.WriteAsync("UNPARSEABLE_VALUE");
                await Task.Delay(10000);
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "REQUEST");
            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.Internal, ex.Status.StatusCode);
        }

        [Test]
        public void RequestParsingError_UnaryRequest()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("RESPONSE");
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "UNPARSEABLE_VALUE"));
            // Spec doesn't define the behavior. With the current implementation server handler throws exception which results in StatusCode.Unknown.
            Assert.AreEqual(StatusCode.Unknown, ex.Status.StatusCode);
        }

        [Test]
        public async Task RequestParsingError_StreamingRequest()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                try
                {
                    // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                    await requestStream.MoveNext();
                    Assert.Fail();
                }
                catch (IOException)
                {
                }
                return "RESPONSE";
            });

            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall());
            await call.RequestStream.WriteAsync("UNPARSEABLE_VALUE");

            Assert.AreEqual("RESPONSE", await call);
        }

        [Test]
        public void RequestSerializationError_BlockingUnary()
        {
            Assert.Throws<IOException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "UNSERIALIZABLE_VALUE"));
        }

        [Test]
        public void RequestSerializationError_AsyncUnary()
        {
            Assert.ThrowsAsync<IOException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "UNSERIALIZABLE_VALUE"));
        }

        [Test]
        public async Task RequestSerializationError_ClientStreaming()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                CollectionAssert.AreEqual(new[] { "A", "B" }, await requestStream.ToListAsync());
                return "RESPONSE";
            });
            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall());
            await call.RequestStream.WriteAsync("A");
            Assert.ThrowsAsync<IOException>(async () => await call.RequestStream.WriteAsync("UNSERIALIZABLE_VALUE"));
            await call.RequestStream.WriteAsync("B");
            await call.RequestStream.CompleteAsync();

            Assert.AreEqual("RESPONSE", await call);
        }
    }
}
