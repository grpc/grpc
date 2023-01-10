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
                }
                catch (RpcException)
                {
                    // don't care about server side exceptions in this test
                }
            });

            // The 'using' statement makes sure that Dispose() is called at the end
            // which is the point of the test - to make sure resources aren't leaked
            // when Dispose() is called.
            using (var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "A B C D E"))
            {
                Assert.AreEqual(1, channel.GetCallReferenceCount());

                try
                {
                    // Just call MoveNext once rather than consuming the entire stream
                    // to check that resources are released when the entire stream hasn't
                    // been read
                    await call.ResponseStream.MoveNext();
                    Assert.AreEqual("A", call.ResponseStream.Current);
                }
                catch (RpcException ex)
                {
                    Assert.Fail("Unexpected exception thrown {}", ex);
                }
            } // Dispose happens here

            // short delay to allow callbacks to fire
            await Task.Delay(500);

            // Resources for the call should have been cleaned up and thus no reference
            // to the call in the channel. This would fail with a count of 1 before the
            // fix for #8451.
            Assert.AreEqual(0, channel.GetCallReferenceCount());
        }

        [Test]
        public async Task ServerStreamingCall_DisposeCallBeforeAnyRead()
        {
            // Test to reproduce https://github.com/grpc/grpc/issues/8451
            // "Client channel socket leaks unless read stream drained explicitly"
            // This version of the test disposes the call before any of the response stream is
            // read.

            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) => {
                try
                {
                    // send responses to the client with a short delay between each response
                    foreach (string resp in request.Split(new[] { ' ' }))
                    {
                        await responseStream.WriteAsync(resp);
                        await Task.Delay(50);
                    }
                }
                catch (RpcException)
                {
                    // don't care about server side exceptions in this test
                }
            });

            // The 'using' statement makes sure that Dispose() is called at the end
            // which is the point of the test - to make sure resources aren't leaked
            // when Dispose() is called.
            using (var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "A B C D E"))
            {
                Assert.AreEqual(1, channel.GetCallReferenceCount());

                // don't read the response steam
            } // Dispose happens here

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
                }
                catch (RpcException)
                {
                    // don't care about server side exceptions in this test
                }
            });

            var tokenSource = new CancellationTokenSource();

            var call = Calls.AsyncServerStreamingCall(
                helper.CreateServerStreamingCall(new CallOptions(cancellationToken: tokenSource.Token)),
                "A B C D E");

            Assert.AreEqual(1, channel.GetCallReferenceCount());

            try
            {
                await call.ResponseStream.MoveNext();
                Assert.AreEqual("A", call.ResponseStream.Current);
                // To reproduce issue #8451 - cancel the call and don't
                // read the response stream to the end
                tokenSource.Cancel();
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
        public async Task ServerStreamingCall_CancelCallBeforeAllReadAndDispose()
        {
            // Test to reproduce https://github.com/grpc/grpc/issues/8451
            // "Client channel socket leaks unless read stream drained explicitly"
            // This version of the test cancels the call before the response stream is
            // fully read. It also calls Dispose on the call after the Cancel to check that
            // the cleanup doesn't happen twice

            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) => {
                try
                {
                    // send responses to the client with a short delay between each response
                    foreach (string resp in request.Split(new[] { ' ' }))
                    {
                        await responseStream.WriteAsync(resp);
                        await Task.Delay(50);
                    }
                }
                catch (RpcException)
                {
                    // don't care about server side exceptions in this test
                }
            });

            var tokenSource = new CancellationTokenSource();

            // 'using' makes sure Dispose happens
            using (var call = Calls.AsyncServerStreamingCall(
                helper.CreateServerStreamingCall(new CallOptions(cancellationToken: tokenSource.Token)),
                "A B C D E"))
            {
                Assert.AreEqual(1, channel.GetCallReferenceCount());

                try
                {
                    await call.ResponseStream.MoveNext();
                    Assert.AreEqual("A", call.ResponseStream.Current);
                    // To reproduce issue #8451 - cancel the call and don't read
                    // the response stream to the end
                    tokenSource.Cancel();
                }
                catch (RpcException ex)
                {
                    Assert.Fail("Unexpected exception thrown {}", ex);
                }
            } // Dispose happens here

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

        [Test]
        public async Task ServerStreamingCall_CancelAfterServerHasFinished()
        {
            // Test to verify the fix for https://github.com/grpc/grpc/issues/17761.
            // Fix appears to have happened in c-core between tags v1.30.0 (broken) and v1.31.0-pre1 (fixed).
            //
            // Scenario: the client cancels the request after the server has already successfully
            // returned the results but before the client starts reading.
            //
            // Expected behaviour is that the client sees a Cancelled status and no messages received.

            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) => {
                await responseStream.WriteAsync("abc");
            });

            var cts = new CancellationTokenSource();
            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(new CallOptions(cancellationToken: cts.Token)), "request1");
            // make sure the response and status sent by the server is received on the client side
            await Task.Delay(2000);

            // cancel the call before actually reading the response
            cts.Cancel();
            var moveNextTask = call.ResponseStream.MoveNext();

            try
            {
                // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                await moveNextTask;
                Assert.Fail();
            }
            catch (RpcException ex)
            {
                // expect a Cancelled status
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
        }
    }

}
