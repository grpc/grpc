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
using System.Runtime.InteropServices;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    /// <summary>
    /// Uses fake native call to test interaction of <c>AsyncCallServer</c> wrapping code with C core in different situations.
    /// </summary>
    public class AsyncCallServerTest
    {
        Server server;
        FakeNativeCall fakeCall;
        AsyncCallServer<string, string> asyncCallServer;

        [SetUp]
        public void Init()
        {
            // Create a fake server just so we have an instance to refer to.
            // The server won't actually be used at all.
            server = new Server()
            {
                Ports = { { "localhost", 0, ServerCredentials.Insecure } }
            };
            server.Start();

            fakeCall = new FakeNativeCall();
            asyncCallServer = new AsyncCallServer<string, string>(
                Marshallers.StringMarshaller.Serializer, Marshallers.StringMarshaller.Deserializer,
                server);
            asyncCallServer.InitializeForTesting(fakeCall);
        }

        [TearDown]
        public void Cleanup()
        {
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void CancelNotificationAfterStartDisposes()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void CancelNotificationAfterStartDisposesAfterPendingReadFinishes()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);

            var moveNextTask = requestStream.MoveNext();

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);
            fakeCall.ReceivedMessageHandler(true, null);
            Assert.IsFalse(moveNextTask.Result);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void ReadAfterCancelNotificationCanSucceed()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);

            // Check that starting a read after cancel notification has been processed is legal.
            var moveNextTask = requestStream.MoveNext();
            Assert.IsFalse(moveNextTask.Result);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void ReadCompletionFailureClosesRequestStream()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);

            // if a read completion's success==false, the request stream will silently finish
            // and we rely on C core cancelling the call.
            var moveNextTask = requestStream.MoveNext();
            fakeCall.ReceivedMessageHandler(false, null);
            Assert.IsFalse(moveNextTask.Result);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteAfterCancelNotificationFails()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);

            // TODO(jtattermusch): should we throw a different exception type instead?
            Assert.Throws(typeof(InvalidOperationException), () => responseStream.WriteAsync("request1"));
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteCompletionFailureThrows()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            var writeTask = responseStream.WriteAsync("request1");
            fakeCall.SendCompletionHandler(false);
            // TODO(jtattermusch): should we throw a different exception type instead?
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await writeTask);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteAndWriteStatusCanRunConcurrently()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            var writeTask = responseStream.WriteAsync("request1");
            var writeStatusTask = asyncCallServer.SendStatusFromServerAsync(Status.DefaultSuccess, new Metadata(), null);

            fakeCall.SendCompletionHandler(true);
            fakeCall.SendStatusFromServerHandler(true);

            Assert.DoesNotThrowAsync(async () => await writeTask);
            Assert.DoesNotThrowAsync(async () => await writeStatusTask);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteAfterWriteStatusThrowsInvalidOperationException()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            asyncCallServer.SendStatusFromServerAsync(Status.DefaultSuccess, new Metadata(), null);
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await responseStream.WriteAsync("request1"));

            fakeCall.SendStatusFromServerHandler(true);
            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        static void AssertFinished(AsyncCallServer<string, string> asyncCallServer, FakeNativeCall fakeCall, Task finishedTask)
        {
            Assert.IsTrue(fakeCall.IsDisposed);
            Assert.IsTrue(finishedTask.IsCompleted);
            Assert.DoesNotThrow(() => finishedTask.Wait());
        }
    }
}
