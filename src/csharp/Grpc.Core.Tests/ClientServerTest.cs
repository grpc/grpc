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
