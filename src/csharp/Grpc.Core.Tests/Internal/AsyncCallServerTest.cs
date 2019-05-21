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
using System.IO;
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
        FakeBufferReaderManager fakeBufferReaderManager;

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
                Marshallers.StringMarshaller.ContextualSerializer, Marshallers.StringMarshaller.ContextualDeserializer,
                server);
            asyncCallServer.InitializeForTesting(fakeCall);
            fakeBufferReaderManager = new FakeBufferReaderManager();
        }

        [TearDown]
        public void Cleanup()
        {
            fakeBufferReaderManager.Dispose();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void CancelNotificationAfterStartDisposes()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void CancelNotificationAfterStartDisposesAfterPendingReadFinishes()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);

            var moveNextTask = requestStream.MoveNext();

            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);
            fakeCall.ReceivedMessageCallback.OnReceivedMessage(true, CreateNullResponse());
            Assert.IsFalse(moveNextTask.Result);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void ReadAfterCancelNotificationCanSucceed()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);

            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);

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
            fakeCall.ReceivedMessageCallback.OnReceivedMessage(false, CreateNullResponse());
            Assert.IsFalse(moveNextTask.Result);

            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteAfterCancelNotificationFails()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);

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
            fakeCall.SendCompletionCallback.OnSendCompletion(false);
            Assert.ThrowsAsync(typeof(IOException), async () => await writeTask);

            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);
            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteAndWriteStatusCanRunConcurrently()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            var writeTask = responseStream.WriteAsync("request1");
            var writeStatusTask = asyncCallServer.SendStatusFromServerAsync(Status.DefaultSuccess, new Metadata(), null);

            fakeCall.SendCompletionCallback.OnSendCompletion(true);
            fakeCall.SendStatusFromServerCallback.OnSendStatusFromServerCompletion(true);

            Assert.DoesNotThrowAsync(async () => await writeTask);
            Assert.DoesNotThrowAsync(async () => await writeStatusTask);

            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void WriteAfterWriteStatusThrowsInvalidOperationException()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            asyncCallServer.SendStatusFromServerAsync(Status.DefaultSuccess, new Metadata(), null);
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await responseStream.WriteAsync("request1"));

            fakeCall.SendStatusFromServerCallback.OnSendStatusFromServerCompletion(true);
            fakeCall.ReceivedCloseOnServerCallback.OnReceivedCloseOnServer(true, cancelled: true);

            AssertFinished(asyncCallServer, fakeCall, finishedTask);
        }

        static void AssertFinished(AsyncCallServer<string, string> asyncCallServer, FakeNativeCall fakeCall, Task finishedTask)
        {
            Assert.IsTrue(fakeCall.IsDisposed);
            Assert.IsTrue(finishedTask.IsCompleted);
            Assert.DoesNotThrow(() => finishedTask.Wait());
        }

        IBufferReader CreateNullResponse()
        {
            return fakeBufferReaderManager.CreateNullPayloadBufferReader();
        }
    }
}
