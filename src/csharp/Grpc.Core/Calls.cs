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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// Helper methods for generated stubs to make RPC calls.
    /// </summary>
    public static class Calls
    {
        public static TResponse BlockingUnaryCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestMarshaller.Serializer, call.ResponseMarshaller.Deserializer);
            return asyncCall.UnaryCall(call.Channel, call.Name, req, call.Headers);
        }

        public static async Task<TResponse> AsyncUnaryCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestMarshaller.Serializer, call.ResponseMarshaller.Deserializer);
            asyncCall.Initialize(call.Channel, GetCompletionQueue(), call.Name);
            return await asyncCall.UnaryCallAsync(req, call.Headers);
        }

        public static void AsyncServerStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, IObserver<TResponse> outputs, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestMarshaller.Serializer, call.ResponseMarshaller.Deserializer);
            asyncCall.Initialize(call.Channel, GetCompletionQueue(), call.Name);
            asyncCall.StartServerStreamingCall(req, outputs, call.Headers);
        }

        public static ClientStreamingAsyncResult<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestMarshaller.Serializer, call.ResponseMarshaller.Deserializer);
            asyncCall.Initialize(call.Channel, GetCompletionQueue(), call.Name);
            var task = asyncCall.ClientStreamingCallAsync(call.Headers);
            var inputs = new ClientStreamingInputObserver<TRequest, TResponse>(asyncCall);
            return new ClientStreamingAsyncResult<TRequest, TResponse>(task, inputs);
        }

        public static TResponse BlockingClientStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObservable<TRequest> inputs, CancellationToken token)
        {
            throw new NotImplementedException();
        }

        public static IObserver<TRequest> DuplexStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObserver<TResponse> outputs, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestMarshaller.Serializer, call.ResponseMarshaller.Deserializer);
            asyncCall.Initialize(call.Channel, GetCompletionQueue(), call.Name);
            asyncCall.StartDuplexStreamingCall(outputs, call.Headers);
            return new ClientStreamingInputObserver<TRequest, TResponse>(asyncCall);
        }

        private static CompletionQueueSafeHandle GetCompletionQueue()
        {
            return GrpcEnvironment.ThreadPool.CompletionQueue;
        }
    }
}
