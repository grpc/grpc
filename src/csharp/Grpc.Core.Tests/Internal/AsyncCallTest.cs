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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    /// <summary>
    /// Uses fake native call to test interaction of <c>AsyncCall</c> wrapping code with C core in different situations.
    /// </summary>
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
                () => asyncCall.SendMessageAsync("abc", new WriteFlags()));
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
                null,
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
                null,
                new Metadata());

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.InvalidArgument);
        }

        [Test]
        public void ClientStreaming_MoreRequests_Success()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            var writeTask = requestStream.WriteAsync("request1");
            fakeCall.SendCompletionHandler(true);
            writeTask.Wait();

            var writeTask2 = requestStream.WriteAsync("request2");
            fakeCall.SendCompletionHandler(true);
            writeTask2.Wait();

            var completeTask = requestStream.CompleteAsync();
            fakeCall.SendCompletionHandler(true);
            completeTask.Wait();

            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseSuccess(asyncCall, fakeCall, resultTask);
        }

        [Test]
        public void ClientStreaming_WriteFailureThrowsRpcException()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            var writeTask = requestStream.WriteAsync("request1");
            fakeCall.SendCompletionHandler(false);

            // The write will wait for call to finish to receive the status code.
            Assert.IsFalse(writeTask.IsCompleted);

            fakeCall.UnaryResponseClientHandler(true,
                CreateClientSideStatus(StatusCode.Internal),
                null,
                new Metadata());

            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(StatusCode.Internal, ex.Status.StatusCode);

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.Internal);
        }

        [Test]
        public void ClientStreaming_WriteFailureThrowsRpcException2()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            var writeTask = requestStream.WriteAsync("request1");

            fakeCall.UnaryResponseClientHandler(true,
                CreateClientSideStatus(StatusCode.Internal),
                null,
                new Metadata());

            fakeCall.SendCompletionHandler(false);

            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(StatusCode.Internal, ex.Status.StatusCode);

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.Internal);
        }

        [Test]
        public void ClientStreaming_WriteFailureThrowsRpcException3()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            var writeTask = requestStream.WriteAsync("request1");
            fakeCall.SendCompletionHandler(false);

            // Until the delayed write completion has been triggered,
            // we still act as if there was an active write.
            Assert.Throws(typeof(InvalidOperationException), () => requestStream.WriteAsync("request2"));

            fakeCall.UnaryResponseClientHandler(true,
                CreateClientSideStatus(StatusCode.Internal),
                null,
                new Metadata());

            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(StatusCode.Internal, ex.Status.StatusCode);

            // Following attempts to write keep delivering the same status
            var ex2 = Assert.ThrowsAsync<RpcException>(async () => await requestStream.WriteAsync("after call has finished"));
            Assert.AreEqual(StatusCode.Internal, ex2.Status.StatusCode);

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.Internal);
        }

        [Test]
        public void ClientStreaming_WriteAfterReceivingStatusThrowsRpcException()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseSuccess(asyncCall, fakeCall, resultTask);

            var writeTask = requestStream.WriteAsync("request1");
            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(Status.DefaultSuccess, ex.Status);
        }

        [Test]
        public void ClientStreaming_WriteAfterReceivingStatusThrowsRpcException2()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(new Status(StatusCode.OutOfRange, ""), new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.OutOfRange);

            var writeTask = requestStream.WriteAsync("request1");
            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(StatusCode.OutOfRange, ex.Status.StatusCode);
        }

        [Test]
        public void ClientStreaming_WriteAfterCompleteThrowsInvalidOperationException()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            requestStream.CompleteAsync();

            Assert.Throws(typeof(InvalidOperationException), () => requestStream.WriteAsync("request1"));

            fakeCall.SendCompletionHandler(true);

            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseSuccess(asyncCall, fakeCall, resultTask);
        }

        [Test]
        public void ClientStreaming_CompleteAfterReceivingStatusSucceeds()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            fakeCall.UnaryResponseClientHandler(true,
                new ClientSideStatus(Status.DefaultSuccess, new Metadata()),
                CreateResponsePayload(),
                new Metadata());

            AssertUnaryResponseSuccess(asyncCall, fakeCall, resultTask);
            Assert.DoesNotThrowAsync(async () => await requestStream.CompleteAsync());
        }

        [Test]
        public void ClientStreaming_WriteAfterCancellationRequestThrowsTaskCanceledException()
        {
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);

            asyncCall.Cancel();
            Assert.IsTrue(fakeCall.IsCancelled);

            var writeTask = requestStream.WriteAsync("request1");
            Assert.ThrowsAsync(typeof(TaskCanceledException), async () => await writeTask);

            fakeCall.UnaryResponseClientHandler(true,
                CreateClientSideStatus(StatusCode.Cancelled),
                null,
                new Metadata());

            AssertUnaryResponseError(asyncCall, fakeCall, resultTask, StatusCode.Cancelled);
        }

        [Test]
        public void ServerStreaming_StreamingSendNotAllowed()
        {
            asyncCall.StartServerStreamingCall("request1");
            Assert.Throws(typeof(InvalidOperationException),
                () => asyncCall.SendMessageAsync("abc", new WriteFlags()));
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

        [Test]
        public void DuplexStreaming_NoRequestNoResponse_Success()
        {
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var writeTask1 = requestStream.CompleteAsync();
            fakeCall.SendCompletionHandler(true);
            Assert.DoesNotThrowAsync(async () => await writeTask1);

            var readTask = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, new ClientSideStatus(Status.DefaultSuccess, new Metadata()));

            AssertStreamingResponseSuccess(asyncCall, fakeCall, readTask);
        }

        [Test]
        public void DuplexStreaming_WriteAfterReceivingStatusThrowsRpcException()
        {
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var readTask = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, new ClientSideStatus(Status.DefaultSuccess, new Metadata()));

            AssertStreamingResponseSuccess(asyncCall, fakeCall, readTask);

            var writeTask = requestStream.WriteAsync("request1");
            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(Status.DefaultSuccess, ex.Status);
        }

        [Test]
        public void DuplexStreaming_CompleteAfterReceivingStatusSuceeds()
        {
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var readTask = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, new ClientSideStatus(Status.DefaultSuccess, new Metadata()));

            AssertStreamingResponseSuccess(asyncCall, fakeCall, readTask);

            Assert.DoesNotThrowAsync(async () => await requestStream.CompleteAsync());
        }

        [Test]
        public void DuplexStreaming_WriteFailureThrowsRpcException()
        {
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var writeTask = requestStream.WriteAsync("request1");
            fakeCall.SendCompletionHandler(false);

            // The write will wait for call to finish to receive the status code.
            Assert.IsFalse(writeTask.IsCompleted);

            var readTask = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, CreateClientSideStatus(StatusCode.PermissionDenied));

            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(StatusCode.PermissionDenied, ex.Status.StatusCode);

            AssertStreamingResponseError(asyncCall, fakeCall, readTask, StatusCode.PermissionDenied);
        }

        [Test]
        public void DuplexStreaming_WriteFailureThrowsRpcException2()
        {
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var writeTask = requestStream.WriteAsync("request1");

            var readTask = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, CreateClientSideStatus(StatusCode.PermissionDenied));
            fakeCall.SendCompletionHandler(false);

            var ex = Assert.ThrowsAsync<RpcException>(async () => await writeTask);
            Assert.AreEqual(StatusCode.PermissionDenied, ex.Status.StatusCode);

            AssertStreamingResponseError(asyncCall, fakeCall, readTask, StatusCode.PermissionDenied);
        }

        [Test]
        public void DuplexStreaming_WriteAfterCancellationRequestThrowsTaskCanceledException()
        {
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<string, string>(asyncCall);
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            asyncCall.Cancel();
            Assert.IsTrue(fakeCall.IsCancelled);

            var writeTask = requestStream.WriteAsync("request1");
            Assert.ThrowsAsync(typeof(TaskCanceledException), async () => await writeTask);

            var readTask = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, CreateClientSideStatus(StatusCode.Cancelled));

            AssertStreamingResponseError(asyncCall, fakeCall, readTask, StatusCode.Cancelled);
        }

        [Test]
        public void DuplexStreaming_ReadAfterCancellationRequestCanSucceed()
        {
            asyncCall.StartDuplexStreamingCall();
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            asyncCall.Cancel();
            Assert.IsTrue(fakeCall.IsCancelled);

            var readTask1 = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, CreateResponsePayload());
            Assert.IsTrue(readTask1.Result);
            Assert.AreEqual("response1", responseStream.Current);

            var readTask2 = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, CreateClientSideStatus(StatusCode.Cancelled));

            AssertStreamingResponseError(asyncCall, fakeCall, readTask2, StatusCode.Cancelled);
        }

        [Test]
        public void DuplexStreaming_ReadStartedBeforeCancellationRequestCanSucceed()
        {
            asyncCall.StartDuplexStreamingCall();
            var responseStream = new ClientResponseStream<string, string>(asyncCall);

            var readTask1 = responseStream.MoveNext();  // initiate the read before cancel request
            asyncCall.Cancel();
            Assert.IsTrue(fakeCall.IsCancelled);

            fakeCall.ReceivedMessageHandler(true, CreateResponsePayload());
            Assert.IsTrue(readTask1.Result);
            Assert.AreEqual("response1", responseStream.Current);

            var readTask2 = responseStream.MoveNext();
            fakeCall.ReceivedMessageHandler(true, null);
            fakeCall.ReceivedStatusOnClientHandler(true, CreateClientSideStatus(StatusCode.Cancelled));

            AssertStreamingResponseError(asyncCall, fakeCall, readTask2, StatusCode.Cancelled);
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
            Assert.AreEqual(expectedStatusCode, ex.Status.StatusCode);
            Assert.AreEqual(expectedStatusCode, asyncCall.GetStatus().StatusCode);
            Assert.AreEqual(0, asyncCall.GetTrailers().Count);
        }
    }
}
