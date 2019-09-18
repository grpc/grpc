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
                Assert.IsNotNull(propagationToken.AsImplOrNull().ParentCall);

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
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                Assert.IsTrue(context.Deadline < deadline.AddHours(1));
                Assert.IsTrue(context.Deadline > deadline.AddHours(-1));
                return Task.FromResult("PASS");
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
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                Assert.AreEqual(DateTime.MaxValue, context.Deadline);
                return Task.FromResult("PASS");
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

        [Test]
        public void ForeignPropagationTokenInterpretedAsNull()
        {
            Assert.IsNull(new ForeignContextPropagationToken().AsImplOrNull());
        }

        [Test]
        public async Task ForeignPropagationTokenIsIgnored()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("PASS");
            });

            var callOptions = new CallOptions(propagationToken: new ForeignContextPropagationToken());
            await Calls.AsyncUnaryCall(helper.CreateUnaryCall(callOptions), "xyz");
        }

        // For testing, represents context propagation token that's not generated by Grpc.Core
        private class ForeignContextPropagationToken : ContextPropagationToken
        {
        }
    }
}
