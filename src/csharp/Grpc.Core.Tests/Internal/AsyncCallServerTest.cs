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
            var environment = GrpcEnvironment.AddRef();
            server = new Server();

            fakeCall = new FakeNativeCall();
            asyncCallServer = new AsyncCallServer<string, string>(
                Marshallers.StringMarshaller.Serializer, Marshallers.StringMarshaller.Deserializer,
                environment,
                server);
            asyncCallServer.InitializeForTesting(fakeCall);
        }

        [TearDown]
        public void Cleanup()
        {
            GrpcEnvironment.Release();
        }

        [Test]
        public void CancelNotificationAfterStartDisposes()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            // Finishing requestStream is needed for dispose to happen.
            var moveNextTask = requestStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            Assert.IsFalse(moveNextTask.Result);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);
            AssertDisposed(asyncCallServer, fakeCall, finishedTask);
        }

        [Test]
        public void ReadAfterCancelNotificationCanSucceed()
        {
            var finishedTask = asyncCallServer.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<string, string>(asyncCallServer);
            var responseStream = new ServerResponseStream<string, string>(asyncCallServer);

            fakeCall.ReceivedCloseOnServerHandler(true, cancelled: true);

            // Check that startin a read after cancel notification has been processed is legal.
            var moveNextTask = requestStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            Assert.IsFalse(moveNextTask.Result);

            AssertDisposed(asyncCallServer, fakeCall, finishedTask);
        }


        // TODO: read completion failure ...

        // TODO: 



        // TODO: write fails...

        // TODO: write completion fails...

        // TODO: cancellation delivered...

        // TODO: cancel notification in the middle of a read...

        // TODO: cancel notification in the middle of a write...

        // TODO: cancellation delivered...

        // TODO: what does writing status do to reads?

        static void AssertDisposed(AsyncCallServer<string, string> asyncCallServer, FakeNativeCall fakeCall, Task finishedTask)
        {
            Assert.IsTrue(fakeCall.IsDisposed);
            Assert.IsTrue(finishedTask.IsCompleted);
            Assert.DoesNotThrow(() => finishedTask.Wait());
        }
    }
}
