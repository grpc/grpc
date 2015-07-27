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
    /// <summary>
    /// Tests for Deadline support.
    /// </summary>
    public class TimeoutsTest
    {
        const string Host = "localhost";
        const string ServiceName = "/tests.Test";

        static readonly Method<string, string> TestMethod = new Method<string, string>(
            MethodType.Unary,
            "/tests.Test/Test",
            Marshallers.StringMarshaller,
            Marshallers.StringMarshaller);

        static readonly ServerServiceDefinition ServiceDefinition = ServerServiceDefinition.CreateBuilder(ServiceName)
            .AddMethod(TestMethod, TestMethodHandler)
            .Build();

        // provides a way how to retrieve an out-of-band result value from server handler
        static TaskCompletionSource<string> stringFromServerHandlerTcs;

        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            server = new Server();
            server.AddServiceDefinition(ServiceDefinition);
            int port = server.AddPort(Host, Server.PickUnusedPort, ServerCredentials.Insecure);
            server.Start();
            channel = new Channel(Host, port, Credentials.Insecure);

            stringFromServerHandlerTcs = new TaskCompletionSource<string>();
        }

        [TearDown]
        public void Cleanup()
        {
            channel.Dispose();
            server.ShutdownAsync().Wait();
        }

        [TestFixtureTearDown]
        public void CleanupClass()
        {
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void InfiniteDeadline()
        {
            // no deadline specified, check server sees infinite deadline
            var internalCall = new Call<string, string>(ServiceName, TestMethod, channel, Metadata.Empty);
            Assert.AreEqual("DATETIME_MAXVALUE", Calls.BlockingUnaryCall(internalCall, "RETURN_DEADLINE", CancellationToken.None));

            // DateTime.MaxValue deadline specified, check server sees infinite deadline
            var internalCall2 = new Call<string, string>(ServiceName, TestMethod, channel, Metadata.Empty, DateTime.MaxValue);
            Assert.AreEqual("DATETIME_MAXVALUE", Calls.BlockingUnaryCall(internalCall2, "RETURN_DEADLINE", CancellationToken.None));
        }

        [Test]
        public void DeadlineTransferredToServer()
        {
            var remainingTimeClient = TimeSpan.FromDays(7);
            var deadline = DateTime.UtcNow + remainingTimeClient;
            Thread.Sleep(1000);
            var internalCall = new Call<string, string>(ServiceName, TestMethod, channel, Metadata.Empty, deadline);

            var serverDeadlineTicksString = Calls.BlockingUnaryCall(internalCall, "RETURN_DEADLINE", CancellationToken.None);
            var serverDeadline = new DateTime(long.Parse(serverDeadlineTicksString), DateTimeKind.Utc);

            // A fairly relaxed check that the deadline set by client and deadline seen by server
            // are in agreement. C core takes care of the work with transferring deadline over the wire,
            // so we don't need an exact check here.
            Assert.IsTrue(Math.Abs((deadline - serverDeadline).TotalMilliseconds) < 5000);
        }

        [Test]
        public void DeadlineInThePast()
        {
            var deadline = DateTime.MinValue;
            var internalCall = new Call<string, string>(ServiceName, TestMethod, channel, Metadata.Empty, deadline);

            try
            {
                Calls.BlockingUnaryCall(internalCall, "TIMEOUT", CancellationToken.None);
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.DeadlineExceeded, e.Status.StatusCode);
            }
        }

        [Test]
        public void DeadlineExceededStatusOnTimeout()
        {
            var deadline = DateTime.UtcNow.Add(TimeSpan.FromSeconds(5));
            var internalCall = new Call<string, string>(ServiceName, TestMethod, channel, Metadata.Empty, deadline);

            try
            {
                Calls.BlockingUnaryCall(internalCall, "TIMEOUT", CancellationToken.None);
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.DeadlineExceeded, e.Status.StatusCode);
            }
        }

        [Test]
        public void ServerReceivesCancellationOnTimeout()
        {
            var deadline = DateTime.UtcNow.Add(TimeSpan.FromSeconds(5));
            var internalCall = new Call<string, string>(ServiceName, TestMethod, channel, Metadata.Empty, deadline);

            try
            {
                Calls.BlockingUnaryCall(internalCall, "CHECK_CANCELLATION_RECEIVED", CancellationToken.None);
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.DeadlineExceeded, e.Status.StatusCode);
            }
            Assert.AreEqual("CANCELLED", stringFromServerHandlerTcs.Task.Result);
        }
            
        private static async Task<string> TestMethodHandler(string request, ServerCallContext context)
        {
            if (request == "TIMEOUT")
            {
                await Task.Delay(60000);
                return "";
            }

            if (request == "RETURN_DEADLINE")
            {
                if (context.Deadline == DateTime.MaxValue)
                {
                    return "DATETIME_MAXVALUE";
                }

                return context.Deadline.Ticks.ToString();
            }

            if (request == "CHECK_CANCELLATION_RECEIVED")
            {
                // wait until cancellation token is fired.
                var tcs = new TaskCompletionSource<object>();
                context.CancellationToken.Register(() => { tcs.SetResult(null); });
                await tcs.Task;
                stringFromServerHandlerTcs.SetResult("CANCELLED");
                return "";
            }

            return "";
        }
    }
}
