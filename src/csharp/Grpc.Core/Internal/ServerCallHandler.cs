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
using System.Linq;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal interface IServerCallHandler
    {
        void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq);
    }

    internal class UnaryRequestServerCallHandler<TRequest, TResponse> : IServerCallHandler
    {
        readonly Method<TRequest, TResponse> method;
        readonly UnaryRequestServerMethod<TRequest, TResponse> handler;

        public UnaryRequestServerCallHandler(Method<TRequest, TResponse> method, UnaryRequestServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer);

            asyncCall.Initialize(call);
           
            var requestObserver = new RecordingObserver<TRequest>();
            var finishedTask = asyncCall.ServerSideCallAsync(requestObserver);

            var request = requestObserver.ToList().Result.Single();
            var responseObserver = new ServerStreamingOutputObserver<TRequest, TResponse>(asyncCall);
            handler(request, responseObserver);

            finishedTask.Wait();
        }
    }

    internal class StreamingRequestServerCallHandler<TRequest, TResponse> : IServerCallHandler
    {
        readonly Method<TRequest, TResponse> method;
        readonly StreamingRequestServerMethod<TRequest, TResponse> handler;

        public StreamingRequestServerCallHandler(Method<TRequest, TResponse> method, StreamingRequestServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer);

            asyncCall.Initialize(call);

            var responseObserver = new ServerStreamingOutputObserver<TRequest, TResponse>(asyncCall);
            var requestObserver = handler(responseObserver);
            var finishedTask = asyncCall.ServerSideCallAsync(requestObserver);
            finishedTask.Wait();
        }
    }

    internal class NoSuchMethodCallHandler : IServerCallHandler
    {
        public void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            // We don't care about the payload type here.
            var asyncCall = new AsyncCallServer<byte[], byte[]>(
                (payload) => payload, (payload) => payload);

            asyncCall.Initialize(call);

            var finishedTask = asyncCall.ServerSideCallAsync(new NullObserver<byte[]>());

            // TODO: check result of the completion status.
            asyncCall.StartSendStatusFromServer(new Status(StatusCode.Unimplemented, "No such method."), new AsyncCompletionDelegate((error) => { }));

            finishedTask.Wait();
        }
    }

    internal class NullObserver<T> : IObserver<T>
    {
        public void OnCompleted()
        {
        }

        public void OnError(Exception error)
        {
        }

        public void OnNext(T value)
        {
        }
    }
}
