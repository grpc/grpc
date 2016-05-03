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
    public class AsyncCallTest
    {
        Channel channel;
        FakeNativeCall fakeCall;
        AsyncCall<string, string> asyncCall;

        [SetUp]
        public void Init()
        {
            channel = new Channel("localhost", ChannelCredentials.Insecure);

            fakeCall = new FakeNativeCall();

            var callDetails = new CallInvocationDetails<string, string>(channel, "someMethod", null, Marshallers.StringMarshaller, Marshallers.StringMarshaller, new CallOptions());
            asyncCall = new AsyncCall<string, string>(callDetails, fakeCall);
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
        }

        [Test]
        public void AsyncUnary_CanBeStartedOnlyOnce()
        {
            asyncCall.UnaryCallAsync("request1");
            Assert.Throws(typeof(InvalidOperationException),
                () => asyncCall.UnaryCallAsync("abc"));
        }

        [Test]
        public void AsyncUnary_StreamingOperationsNotAllowed()
        {
            asyncCall.UnaryCallAsync("request1");
            Assert.ThrowsAsync(typeof(InvalidOperationException),
                async () => await asyncCall.ReadMessageAsync());
            Assert.Throws(typeof(InvalidOperationException),
                () => asyncCall.StartSendMessage("abc", new WriteFlags(), (x,y) => {}));
        }

        [Test]
        public void AsyncUnary_Success()
        {
            var resultTask = asyncCall.UnaryCallAsync("request1");
            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseSuccess(asyncCall, fakeCall, resultTask);
        }

        [Test]
        public void AsyncUnary_NonSuccessStatusCode()
        {
            var resultTask = asyncCall.UnaryCallAsync("request1");
            fakeCall.UnaryResponseClientHandler(true,
                CreateClientSideStatus(StatusCode.InvalidArgument),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.InvalidArgument);
        }

        [Test]
        public void AsyncUnary_NullResponsePayload()
        {
            var resultTask = asyncCall.UnaryCallAsync("request1");
            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                null,
                new Metadata());

            // failure to deserialize will result in InvalidArgument status.
            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.Internal);
        }

        [Test]
        public void ClientStreaming_StreamingReadNotAllowed()
        {
            asyncCall.ClientStreamingCallAsync();
            Assert.ThrowsAsync(typeof(InvalidOperationException),
                async () => await asyncCall.ReadMessageAsync());
        }

        [Test]
        public void ClientStreaming_NoRequest_Success()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseSuccess(asyncCall, fakeCall, resultTask);
        }

        [Test]
        public void ClientStreaming_NoRequest_NonSuccessStatusCode()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            fakeCall.UnaryResponseClientHandler(true,
                CreateClientSideStatus(StatusCode.InvalidArgument),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.InvalidArgument);
        }

        [Test]
        public void ServerStreaming_StreamingSendNotAllowed()
        {
            asyncCall.StartServerStreamingCall("request1");
            Assert.Throws(typeof(InvalidOperationException),
                () => asyncCall.StartSendMessage("abc", new WriteFlags(), (x,y) => {}));
        }

        [Test]
        public void ServerStreaming_NoResponse_Success1()
        {
            asyncCall.StartServerStreamingCall("request1");
            var responseStream = new ClientResponseStream<string, string>(asyncCall);
            var readTask = responseStream.MoveNext();

            fakeCall.ReceivedResponseHeadersHandler(true, new Metadata());
            Assert.AreEqual(0, asyncCall.ResponseHeadersAsync.Result.Count);

            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, new ClientSideStatus(Status.DefaultSuccess, new Metadata()));

            AssertStreamingResponseSuccess(asyncCall, fakeCall, readTask);
        }

        [Test]
        public void ServerStreaming_NoResponse_Success2()
        {
            asyncCall.StartServerStreamingCall("request1");
            var responseStream = new ClientResponseStream<string, string>(asyncCall);
            var readTask = responseStream.MoveNext();

            // try alternative order of completions
            fakeCall.ReceivedStatusOnClientHandler(true, new ClientSideStatus(Status.DefaultSuccess, new Metadata()));
            fakeCall.ReceivedMessageHandler(true, null);

            AssertStreamingResponseSuccess(asyncCall, fakeCall, readTask);
        }

        [Test]
        public void ServerStreaming_NoResponse_ReadFailure()
        {
            asyncCall.StartServerStreamingCall("request1");
            var responseStream = new ClientResponseStream<string, string>(asyncCall);
            var readTask = responseStream.MoveNext();

            fakeCall.ReceivedMessageHandler(false, null);  // after a failed read, we rely on C core to deliver appropriate status code.
            fakeCall.ReceivedStatusOnClientHandler(true, CreateClientSideStatus(StatusCode.Internal));

            AssertStreamingResponseError(asyncCall, fakeCall, readTask, StatusCode.Internal);
        }

        [Test]
        public void ServerStreaming_MoreResponses_Success()
        {
            asyncCall.StartServerStreamingCall("request1");
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var readTask1 = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, CreateResponsePayload());
            Assert.IsTrue(readTask1.Result);
            Assert.AreEqual("response1", responseStream.Current);

            var readTask2 = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, CreateResponsePayload());
            Assert.IsTrue(readTask2.Result);
            Assert.AreEqual("response1", responseStream.Current);

            var readTask3 = responseStream.MoveNext();
            fakeCall.ReceivedStatusOnClientHandler(true, new ClientSideStatus(Status.DefaultSuccess, new Metadata()));
            fakeCall.ReceivedMessageHandler(true, null);

            AssertStreamingResponseSuccess(asyncCall, fakeCall, readTask3);
        }

        ClientSideStatus CreateClientSideStatus(StatusCode statusCode)
        {
            return new ClientSideStatus(new Status(statusCode, ""), new Metadata());
        }

        byte[] CreateResponsePayload()
        {
            return Marshallers.StringMarshaller.Serializer("response1");
        }

        static void AssertUnaryResponseSuccess(AsyncCall<string, string> asyncCall, FakeNativeCall fakeCall, Task<string> resultTask)
        {
            Assert.IsTrue(resultTask.IsCompleted);
            Assert.IsTrue(fakeCall.IsDisposed);

            Assert.AreEqual(Status.DefaultSuccess, asyncCall.GetStatus());
            Assert.AreEqual(0, asyncCall.ResponseHeadersAsync.Result.Count);
            Assert.AreEqual(0, asyncCall.GetTrailers().Count);
            Assert.AreEqual("response1", resultTask.Result);
        }

        static void AssertStreamingResponseSuccess(AsyncCall<string, string> asyncCall, FakeNativeCall fakeCall, Task<bool> moveNextTask)
        {
            Assert.IsTrue(moveNextTask.IsCompleted);
            Assert.IsTrue(fakeCall.IsDisposed);

            Assert.IsFalse(moveNextTask.Result);
            Assert.AreEqual(Status.DefaultSuccess, asyncCall.GetStatus());
            Assert.AreEqual(0, asyncCall.GetTrailers().Count);
        }

        static void AssertUnaryResponseError(AsyncCall<string, string> asyncCall, FakeNativeCall fakeCall, Task<string> resultTask, StatusCode expectedStatusCode)
        {
            Assert.IsTrue(resultTask.IsCompleted);
            Assert.IsTrue(fakeCall.IsDisposed);

            Assert.AreEqual(expectedStatusCode, asyncCall.GetStatus().StatusCode);
            var ex = Assert.ThrowsAsync<RpcException>(async () => await resultTask);
            Assert.AreEqual(expectedStatusCode, ex.Status.StatusCode);
            Assert.AreEqual(0, asyncCall.ResponseHeadersAsync.Result.Count);
            Assert.AreEqual(0, asyncCall.GetTrailers().Count);
        }

        static void AssertStreamingResponseError(AsyncCall<string, string> asyncCall, FakeNativeCall fakeCall, Task<bool> moveNextTask, StatusCode expectedStatusCode)
        {
            Assert.IsTrue(moveNextTask.IsCompleted);
            Assert.IsTrue(fakeCall.IsDisposed);

            var ex = Assert.ThrowsAsync<RpcException>(async () => await moveNextTask);
            Assert.AreEqual(expectedStatusCode, asyncCall.GetStatus().StatusCode);
            Assert.AreEqual(0, asyncCall.GetTrailers().Count);
        }

        internal class FakeNativeCall : INativeCall
        {
            public UnaryResponseClientHandler UnaryResponseClientHandler
            {
                get;
                set;
            }

            public ReceivedStatusOnClientHandler ReceivedStatusOnClientHandler
            {
                get;
                set;
            }

            public ReceivedMessageHandler ReceivedMessageHandler
            {
                get;
                set;
            }

            public ReceivedResponseHeadersHandler ReceivedResponseHeadersHandler
            {
                get;
                set;
            }

            public SendCompletionHandler SendCompletionHandler
            {
                get;
                set;
            }

            public ReceivedCloseOnServerHandler ReceivedCloseOnServerHandler
            {
                get;
                set;
            }

            public bool IsCancelled
            {
                get;
                set;
            }

            public bool IsDisposed
            {
                get;
                set;
            }

            public void Cancel()
            {
                IsCancelled = true;
            }

            public void CancelWithStatus(Status status)
            {
                IsCancelled = true;
            }

            public string GetPeer()
            {
                return "PEER";
            }

            public void StartUnary(UnaryResponseClientHandler callback, byte[] payload, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags)
            {
                UnaryResponseClientHandler = callback;
            }

            public void StartUnary(BatchContextSafeHandle ctx, byte[] payload, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags)
            {
                throw new NotImplementedException();
            }

            public void StartClientStreaming(UnaryResponseClientHandler callback, MetadataArraySafeHandle metadataArray)
            {
                UnaryResponseClientHandler = callback;
            }

            public void StartServerStreaming(ReceivedStatusOnClientHandler callback, byte[] payload, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags)
            {
                ReceivedStatusOnClientHandler = callback;
            }

            public void StartDuplexStreaming(ReceivedStatusOnClientHandler callback, MetadataArraySafeHandle metadataArray)
            {
                ReceivedStatusOnClientHandler = callback;
            }

            public void StartReceiveMessage(ReceivedMessageHandler callback)
            {
                ReceivedMessageHandler = callback;
            }

            public void StartReceiveInitialMetadata(ReceivedResponseHeadersHandler callback)
            {
                ReceivedResponseHeadersHandler = callback;
            }

            public void StartSendInitialMetadata(SendCompletionHandler callback, MetadataArraySafeHandle metadataArray)
            {
                SendCompletionHandler = callback;
            }

            public void StartSendMessage(SendCompletionHandler callback, byte[] payload, WriteFlags writeFlags, bool sendEmptyInitialMetadata)
            {
                SendCompletionHandler = callback;
            }

            public void StartSendCloseFromClient(SendCompletionHandler callback)
            {
                SendCompletionHandler = callback;
            }

            public void StartSendStatusFromServer(SendCompletionHandler callback, Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata)
            {
                SendCompletionHandler = callback;
            }

            public void StartServerSide(ReceivedCloseOnServerHandler callback)
            {
                ReceivedCloseOnServerHandler = callback;
            }

            public void Dispose()
            {
                IsDisposed = true;
            }
        }
    }
}
