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
    public class CallCancellationTest
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
        public async Task ClientStreamingCall_CancelServerSideRead()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                var cts = new CancellationTokenSource();
                var moveNextTask = requestStream.MoveNext(cts.Token);
                cts.Cancel();
                await moveNextTask;
                return "";
            });

            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall());
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
        public async Task ServerStreamingCall_CancelClientSideRead()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                await responseStream.WriteAsync("abc");
                while (!context.CancellationToken.IsCancellationRequested)
                {
                    await Task.Delay(10);
                }
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");
            await call.ResponseStream.MoveNext();
            Assert.AreEqual("abc", call.ResponseStream.Current);

            var cts = new CancellationTokenSource();
            var moveNextTask = call.ResponseStream.MoveNext(cts.Token);
            cts.Cancel();

            try
            {
                // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                await moveNextTask;
                Assert.Fail();
            }
            catch (RpcException ex)
            {
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
        }

        [Test]
        public async Task ServerStreamingCall_DisposeCallBeforeAllRead()
        {
            // Test to reproduce https://github.com/grpc/grpc/issues/8451
            // "Client channel socket leaks unless read stream drained explicitly"
            // This version of the test disposes the call before the response stream is
            // fully read.

            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) => {
                try
                {
                    // send responses to the client with a short delay between each response
                    foreach (string resp in request.Split(new[] { ' ' }))
                    {
                        await responseStream.WriteAsync(resp);
                        await Task.Delay(50);
                    }
                    context.ResponseTrailers.Add("xyz", "");
                }
                catch (RpcException ex)
                {
                    // server may get an exception when the client cancels
                    Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
                }
            });

            using (var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "A B C D E"))
            {
                Assert.AreEqual(1, channel.GetCallReferenceCount());

                try
                {
                    while (await call.ResponseStream.MoveNext())
                    {
                        Assert.AreEqual("A", call.ResponseStream.Current);
                        // To reproduce issue #8451 - leave the loop early and ignore other responses
                        break;
                    }
                }
                catch (RpcException ex)
                {
                    Assert.Fail("Unexpected exception thrown {}", ex);
                }
            }
            
            // short delay to allow callbacks to fire
            await Task.Delay(500);

            // Resources for the call should have been cleaned up and thus no reference
            // to the call in the channel. This would fail with a count of 1 before the
            // fix for #8451.
            Assert.AreEqual(0, channel.GetCallReferenceCount());
        }

        [Test]
        public async Task ServerStreamingCall_CancelCallBeforeAllRead()
        {
            // Test to reproduce https://github.com/grpc/grpc/issues/8451
            // "Client channel socket leaks unless read stream drained explicitly"
            // This version of the test cancels the call before the response stream is
            // fully read.

            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) => {
                try
                {
                    // send responses to the client with a short delay between each response
                    foreach (string resp in request.Split(new[] { ' ' }))
                    {
                        await responseStream.WriteAsync(resp);
                        await Task.Delay(50);
                    }
                    context.ResponseTrailers.Add("xyz", "");
                }
                catch (RpcException ex)
                {
                    // server may get an exception when the client cancels
                    Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
                }
            });

            var tokenSource = new CancellationTokenSource();

            var call = Calls.AsyncServerStreamingCall(
                helper.CreateServerStreamingCall(new CallOptions(cancellationToken: tokenSource.Token)),
                "A B C D E");
            
            Assert.AreEqual(1, channel.GetCallReferenceCount());

            try
            {
                while (await call.ResponseStream.MoveNext())
                {
                    Assert.AreEqual("A", call.ResponseStream.Current);
                    // To reproduce issue #8451 - cancel the call and leave
                    // the loop early and ignore other responses
                    tokenSource.Cancel();
                    break;
                }
            }
            catch (RpcException ex)
            {
                Assert.Fail("Unexpected exception thrown {}", ex);
            }
            
            // short delay to allow callbacks to fire
            await Task.Delay(500);

            // Note: this version of the test does not explicitly Dispose the call. It
            // is assuming the cancel of the call triggers the cleanup.

            // Resources for the call should have been cleaned up and thus no reference
            // to the call in the channel. This would fail with a count of 1 before the
            // fix for #8451.
            Assert.AreEqual(0, channel.GetCallReferenceCount());
        }

        [Test]
        public void CanDisposeDefaultCancellationRegistration()
        {
            // prove that we're fine to dispose default CancellationTokenRegistration
            // values without boxing them to IDisposable for a null-check
            var obj = default(CancellationTokenRegistration);
            obj.Dispose();

            using (obj) {}
        }
    }
}
