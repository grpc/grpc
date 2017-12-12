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
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task UnaryCall()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult(request);
            });

            Assert.AreEqual("ABC", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "ABC"));

            Assert.AreEqual("ABC", await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "ABC"));
        }

        [Test]
        public void UnaryCall_ServerHandlerThrows()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                throw new Exception("This was thrown on purpose by a test");
            });
                
            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unknown, ex.Status.StatusCode); 

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unknown, ex2.Status.StatusCode);
        }

        [Test]
        public void UnaryCall_ServerHandlerThrowsRpcException()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                throw new RpcException(new Status(StatusCode.Unauthenticated, ""));
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex.Status.StatusCode);
            Assert.AreEqual(0, ex.Trailers.Count);

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex2.Status.StatusCode);
            Assert.AreEqual(0, ex.Trailers.Count);
        }

        [Test]
        public void UnaryCall_ServerHandlerThrowsRpcExceptionWithTrailers()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var trailers = new Metadata { {"xyz", "xyz-value"} };
                throw new RpcException(new Status(StatusCode.Unauthenticated, ""), trailers);
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex.Status.StatusCode);
            Assert.AreEqual(1, ex.Trailers.Count);
            Assert.AreEqual("xyz", ex.Trailers[0].Key);
            Assert.AreEqual("xyz-value", ex.Trailers[0].Value);

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex2.Status.StatusCode);
            Assert.AreEqual(1, ex2.Trailers.Count);
            Assert.AreEqual("xyz", ex2.Trailers[0].Key);
            Assert.AreEqual("xyz-value", ex2.Trailers[0].Value);
        }

        [Test]
        public void UnaryCall_ServerHandlerSetsStatus()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                context.Status = new Status(StatusCode.Unauthenticated, "");
                return Task.FromResult("");
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex.Status.StatusCode);
            Assert.AreEqual(0, ex.Trailers.Count);

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex2.Status.StatusCode);
            Assert.AreEqual(0, ex2.Trailers.Count);
        }

        [Test]
        public void UnaryCall_ServerHandlerSetsStatusAndTrailers()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                context.Status = new Status(StatusCode.Unauthenticated, "");
                context.ResponseTrailers.Add("xyz", "xyz-value");
                return Task.FromResult("");
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex.Status.StatusCode);
            Assert.AreEqual(1, ex.Trailers.Count);
            Assert.AreEqual("xyz", ex.Trailers[0].Key);
            Assert.AreEqual("xyz-value", ex.Trailers[0].Value);

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex2.Status.StatusCode);
            Assert.AreEqual(1, ex2.Trailers.Count);
            Assert.AreEqual("xyz", ex2.Trailers[0].Key);
            Assert.AreEqual("xyz-value", ex2.Trailers[0].Value);
        }

        [Test]
        public async Task ClientStreamingCall()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                string result = "";
                await requestStream.ForEachAsync((request) =>
                {
                    result += request;
                    return TaskUtils.CompletedTask;
                });
                await Task.Delay(100);
                return result;
            });

            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall());
            await call.RequestStream.WriteAllAsync(new string[] { "A", "B", "C" });
            Assert.AreEqual("ABC", await call.ResponseAsync);

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
            Assert.IsNotNull(call.GetTrailers());
        }

        [Test]
        public async Task ServerStreamingCall()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                await responseStream.WriteAllAsync(request.Split(new []{' '}));
                context.ResponseTrailers.Add("xyz", "");
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "A B C");
            CollectionAssert.AreEqual(new string[] { "A", "B", "C" }, await call.ResponseStream.ToListAsync());

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
            Assert.AreEqual("xyz", call.GetTrailers()[0].Key);
        }

        [Test]
        public async Task ServerStreamingCall_EndOfStreamIsIdempotent()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>((request, responseStream, context) => TaskUtils.CompletedTask);

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");

            Assert.IsFalse(await call.ResponseStream.MoveNext());
            Assert.IsFalse(await call.ResponseStream.MoveNext());
        }

        [Test]
        public void ServerStreamingCall_ErrorCanBeAwaitedTwice()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>((request, responseStream, context) =>
            {
                context.Status = new Status(StatusCode.InvalidArgument, "");
                return TaskUtils.CompletedTask;
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.InvalidArgument, ex.Status.StatusCode);

            // attempting MoveNext again should result in throwing the same exception.
            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.InvalidArgument, ex2.Status.StatusCode);
        }

        [Test]
        public void ServerStreamingCall_TrailersFromMultipleSourcesGetConcatenated()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>((request, responseStream, context) =>
            {
                context.ResponseTrailers.Add("xyz", "xyz-value");
                throw new RpcException(new Status(StatusCode.InvalidArgument, ""), new Metadata { {"abc", "abc-value"} });
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.InvalidArgument, ex.Status.StatusCode);
            Assert.AreEqual(2, call.GetTrailers().Count);
            Assert.AreEqual(2, ex.Trailers.Count);
            Assert.AreEqual("xyz", ex.Trailers[0].Key);
            Assert.AreEqual("xyz-value", ex.Trailers[0].Value);
            Assert.AreEqual("abc", ex.Trailers[1].Key);
            Assert.AreEqual("abc-value", ex.Trailers[1].Value);
        }

        [Test]
        public async Task DuplexStreamingCall()
        {
            helper.DuplexStreamingHandler = new DuplexStreamingServerMethod<string, string>(async (requestStream, responseStream, context) =>
            {
                while (await requestStream.MoveNext())
                {
                    await responseStream.WriteAsync(requestStream.Current);
                }
                context.ResponseTrailers.Add("xyz", "xyz-value");
            });

            var call = Calls.AsyncDuplexStreamingCall(helper.CreateDuplexStreamingCall());
            await call.RequestStream.WriteAllAsync(new string[] { "A", "B", "C" });
            CollectionAssert.AreEqual(new string[] { "A", "B", "C" }, await call.ResponseStream.ToListAsync());

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
            Assert.AreEqual("xyz-value", call.GetTrailers()[0].Value);
        }

        [Test]
        public async Task AsyncUnaryCall_EchoMetadata()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                foreach (Metadata.Entry metadataEntry in context.RequestHeaders)
                {
                    if (metadataEntry.Key != "user-agent")
                    {
                        context.ResponseTrailers.Add(metadataEntry);
                    }
                }
                return Task.FromResult("");
            });

            var headers = new Metadata
            {
                { "ascii-header", "abcdefg" },
                { "binary-header-bin", new byte[] { 1, 2, 3, 0, 0xff } }
            };
            var call = Calls.AsyncUnaryCall(helper.CreateUnaryCall(new CallOptions(headers: headers)), "ABC");
            await call;

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);

            var trailers = call.GetTrailers();
            Assert.AreEqual(2, trailers.Count);
            Assert.AreEqual(headers[0].Key, trailers[0].Key);
            Assert.AreEqual(headers[0].Value, trailers[0].Value);

            Assert.AreEqual(headers[1].Key, trailers[1].Key);
            CollectionAssert.AreEqual(headers[1].ValueBytes, trailers[1].ValueBytes);
        }

        [Test]
        public void UnknownMethodHandler()
        {
            var nonexistentMethod = new Method<string, string>(
                MethodType.Unary,
                MockServiceHelper.ServiceName,
                "NonExistentMethod",
                Marshallers.StringMarshaller,
                Marshallers.StringMarshaller);

            var callDetails = new CallInvocationDetails<string, string>(channel, nonexistentMethod, new CallOptions());

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(callDetails, "abc"));
            Assert.AreEqual(StatusCode.Unimplemented, ex.Status.StatusCode);
        }

        [Test]
        public void StatusDetailIsUtf8()
        {
            // some japanese and chinese characters
            var nonAsciiString = "\u30a1\u30a2\u30a3 \u62b5\u6297\u662f\u5f92\u52b3\u7684";
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                context.Status = new Status(StatusCode.Unknown, nonAsciiString);
                return Task.FromResult("");
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unknown, ex.Status.StatusCode);
            Assert.AreEqual(nonAsciiString, ex.Status.Detail);
        }

        [Test]
        public void ServerCallContext_PeerInfoPresent()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult(context.Peer);
            });

            string peer = Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc");
            Assert.IsTrue(peer.Contains(Host));
        }

        [Test]
        public void ServerCallContext_HostAndMethodPresent()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                Assert.IsTrue(context.Host.Contains(Host));
                Assert.AreEqual("/tests.Test/Unary", context.Method);
                return Task.FromResult("PASS");
            });
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
        }

        [Test]
        public void ServerCallContext_AuthContextNotPopulated()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                Assert.IsFalse(context.AuthContext.IsPeerAuthenticated);
                Assert.AreEqual(0, context.AuthContext.Properties.Count());
                return Task.FromResult("PASS");
            });
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
        }
    }
}
