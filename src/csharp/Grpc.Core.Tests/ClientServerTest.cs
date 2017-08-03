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
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                return request;
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

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex2.Status.StatusCode);
        }

        [Test]
        public void UnaryCall_ServerHandlerSetsStatus()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                context.Status = new Status(StatusCode.Unauthenticated, "");
                return "";
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex.Status.StatusCode);

            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unauthenticated, ex2.Status.StatusCode);
        }

        [Test]
        public async Task ClientStreamingCall()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                string result = "";
                await requestStream.ForEachAsync(async (request) =>
                {
                    result += request;
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
            Assert.IsNotNull("xyz", call.GetTrailers()[0].Key);
        }

        [Test]
        public async Task ServerStreamingCall_EndOfStreamIsIdempotent()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");

            Assert.IsFalse(await call.ResponseStream.MoveNext());
            Assert.IsFalse(await call.ResponseStream.MoveNext());
        }

        [Test]
        public async Task ServerStreamingCall_ErrorCanBeAwaitedTwice()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                context.Status = new Status(StatusCode.InvalidArgument, "");
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");

            var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.InvalidArgument, ex.Status.StatusCode);

            // attempting MoveNext again should result in throwing the same exception.
            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.MoveNext());
            Assert.AreEqual(StatusCode.InvalidArgument, ex2.Status.StatusCode);
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
            Assert.IsNotNull("xyz-value", call.GetTrailers()[0].Value);
        }

        [Test]
        public async Task ClientStreamingCall_CancelAfterBegin()
        {
            var barrier = new TaskCompletionSource<object>();

            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                barrier.SetResult(null);
                await requestStream.ToListAsync();
                return "";
            });

            var cts = new CancellationTokenSource();
            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall(new CallOptions(cancellationToken: cts.Token)));

            await barrier.Task;  // make sure the handler has started.
            cts.Cancel();

            try
            {
                // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                await call.ResponseAsync;
                Assert.Fail();
            }
            catch (RpcException ex)
            {
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
        }

        [Test]
        public async Task ClientStreamingCall_ServerSideReadAfterCancelNotificationReturnsNull()
        {
            var handlerStartedBarrier = new TaskCompletionSource<object>();
            var cancelNotificationReceivedBarrier = new TaskCompletionSource<object>();
            var successTcs = new TaskCompletionSource<string>();

            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                handlerStartedBarrier.SetResult(null);

                // wait for cancellation to be delivered.
                context.CancellationToken.Register(() => cancelNotificationReceivedBarrier.SetResult(null));
                await cancelNotificationReceivedBarrier.Task;

                var moveNextResult = await requestStream.MoveNext();
                successTcs.SetResult(!moveNextResult ? "SUCCESS" : "FAIL");
                return "";
            });

            var cts = new CancellationTokenSource();
            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall(new CallOptions(cancellationToken: cts.Token)));

            await handlerStartedBarrier.Task;
            cts.Cancel();

            try
            {
                await call.ResponseAsync;
                Assert.Fail();
            }
            catch (RpcException ex)
            {
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
            Assert.AreEqual("SUCCESS", await successTcs.Task);
        }

        [Test]
        public async Task AsyncUnaryCall_EchoMetadata()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                foreach (Metadata.Entry metadataEntry in context.RequestHeaders)
                {
                    if (metadataEntry.Key != "user-agent")
                    {
                        context.ResponseTrailers.Add(metadataEntry);
                    }
                }
                return "";
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
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                context.Status = new Status(StatusCode.Unknown, nonAsciiString);
                return "";
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
            Assert.AreEqual(StatusCode.Unknown, ex.Status.StatusCode);
            Assert.AreEqual(nonAsciiString, ex.Status.Detail);
        }

        [Test]
        public void ServerCallContext_PeerInfoPresent()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                return context.Peer;
            });

            string peer = Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc");
            Assert.IsTrue(peer.Contains(Host));
        }

        [Test]
        public void ServerCallContext_HostAndMethodPresent()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                Assert.IsTrue(context.Host.Contains(Host));
                Assert.AreEqual("/tests.Test/Unary", context.Method);
                return "PASS";
            });
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
        }

        [Test]
        public void ServerCallContext_AuthContextNotPopulated()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                Assert.IsFalse(context.AuthContext.IsPeerAuthenticated);
                Assert.AreEqual(0, context.AuthContext.Properties.Count());
                return "PASS";
            });
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "abc"));
        }

        [Test]
        public async Task Channel_WaitForStateChangedAsync()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                return request;
            });

            Assert.ThrowsAsync(typeof(TaskCanceledException), 
                async () => await channel.WaitForStateChangedAsync(channel.State, DateTime.UtcNow.AddMilliseconds(10)));

            var stateChangedTask = channel.WaitForStateChangedAsync(channel.State);

            await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "abc");

            await stateChangedTask;
            Assert.AreEqual(ChannelState.Ready, channel.State);
        }

        [Test]
        public async Task Channel_ConnectAsync()
        {
            await channel.ConnectAsync();
            Assert.AreEqual(ChannelState.Ready, channel.State);

            await channel.ConnectAsync(DateTime.UtcNow.AddMilliseconds(1000));
            Assert.AreEqual(ChannelState.Ready, channel.State);
        }
    }
}
