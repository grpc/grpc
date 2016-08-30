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
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ContextPropagationTest
    {
        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper();

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
        public async Task PropagateCancellation()
        {
            var readyToCancelTcs = new TaskCompletionSource<object>();
            var successTcs = new TaskCompletionSource<string>();

            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                readyToCancelTcs.SetResult(null);  // child call running, ready to parent call

                while (!context.CancellationToken.IsCancellationRequested)
                {
                    await Task.Delay(10);
                }
                successTcs.SetResult("CHILD_CALL_CANCELLED");
                return "";
            });

            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                var propagationToken = context.CreatePropagationToken();
                Assert.IsNotNull(propagationToken.ParentCall);

                var callOptions = new CallOptions(propagationToken: propagationToken);
                try
                {
                    await Calls.AsyncUnaryCall(helper.CreateUnaryCall(callOptions), "xyz");
                }
                catch(RpcException)
                {
                    // Child call will get cancelled, eat the exception.
                }
                return "";
            });
                
            var cts = new CancellationTokenSource();
            var parentCall = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall(new CallOptions(cancellationToken: cts.Token)));
            await readyToCancelTcs.Task;
            cts.Cancel();
            try
            {
                // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                await parentCall;
                Assert.Fail();
            }
            catch (RpcException)
            {
            }
            Assert.AreEqual("CHILD_CALL_CANCELLED", await successTcs.Task);
        }

        [Test]
        public async Task PropagateDeadline()
        {
            var deadline = DateTime.UtcNow.AddDays(7);
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                Assert.IsTrue(context.Deadline < deadline.AddMinutes(1));
                Assert.IsTrue(context.Deadline > deadline.AddMinutes(-1));
                return "PASS";
            });

            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                Assert.Throws(typeof(ArgumentException), () =>
                {
                    // Trying to override deadline while propagating deadline from parent call will throw.
                    Calls.BlockingUnaryCall(helper.CreateUnaryCall(
                        new CallOptions(deadline: DateTime.UtcNow.AddDays(8),
                                        propagationToken: context.CreatePropagationToken())), "");
                });

                var callOptions = new CallOptions(propagationToken: context.CreatePropagationToken());
                return await Calls.AsyncUnaryCall(helper.CreateUnaryCall(callOptions), "xyz");
            });
                
            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall(new CallOptions(deadline: deadline)));
            await call.RequestStream.CompleteAsync();
            Assert.AreEqual("PASS", await call);
        }

        [Test]
        public async Task SuppressDeadlinePropagation()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                Assert.AreEqual(DateTime.MaxValue, context.Deadline);
                return "PASS";
            });

            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                Assert.IsTrue(context.CancellationToken.CanBeCanceled);

                var callOptions = new CallOptions(propagationToken: context.CreatePropagationToken(new ContextPropagationOptions(propagateDeadline: false)));
                return await Calls.AsyncUnaryCall(helper.CreateUnaryCall(callOptions), "xyz");
            });

            var cts = new CancellationTokenSource();
            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall(new CallOptions(deadline: DateTime.UtcNow.AddDays(7))));
            await call.RequestStream.CompleteAsync();
            Assert.AreEqual("PASS", await call);
        }
    }
}
