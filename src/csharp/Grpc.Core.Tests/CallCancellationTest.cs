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
