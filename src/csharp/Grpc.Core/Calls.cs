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

using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// Helper methods for generated clients to make RPC calls.
    /// Most users will use this class only indirectly and will be 
    /// making calls using client object generated from protocol
    /// buffer definition files.
    /// </summary>
    public static class Calls
    {
        /// <summary>
        /// Invokes a simple remote call in a blocking fashion.
        /// </summary>
        /// <returns>The response.</returns>
        /// <param name="call">The call defintion.</param>
        /// <param name="req">Request message.</param>
        /// <typeparam name="TRequest">Type of request message.</typeparam>
        /// <typeparam name="TResponse">The of response message.</typeparam>
        public static TResponse BlockingUnaryCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call, TRequest req)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            return asyncCall.UnaryCall(req);
        }

        /// <summary>
        /// Invokes a simple remote call asynchronously.
        /// </summary>
        /// <returns>An awaitable call object providing access to the response.</returns>
        /// <param name="call">The call defintion.</param>
        /// <param name="req">Request message.</param>
        /// <typeparam name="TRequest">Type of request message.</typeparam>
        /// <typeparam name="TResponse">The of response message.</typeparam>
        public static AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call, TRequest req)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            var asyncResult = asyncCall.UnaryCallAsync(req);
            return new AsyncUnaryCall<TResponse>(asyncResult, asyncCall.ResponseHeadersAsync, asyncCall.GetStatus, asyncCall.GetTrailers, asyncCall.Cancel);
        }

        /// <summary>
        /// Invokes a server streaming call asynchronously.
        /// In server streaming scenario, client sends on request and server responds with a stream of responses.
        /// </summary>
        /// <returns>A call object providing access to the asynchronous response stream.</returns>
        /// <param name="call">The call defintion.</param>
        /// <param name="req">Request message.</param>
        /// <typeparam name="TRequest">Type of request message.</typeparam>
        /// <typeparam name="TResponse">The of response messages.</typeparam>
        public static AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call, TRequest req)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            asyncCall.StartServerStreamingCall(req);
            var responseStream = new ClientResponseStream<TRequest, TResponse>(asyncCall);
            return new AsyncServerStreamingCall<TResponse>(responseStream, asyncCall.ResponseHeadersAsync, asyncCall.GetStatus, asyncCall.GetTrailers, asyncCall.Cancel);
        }

        /// <summary>
        /// Invokes a client streaming call asynchronously.
        /// In client streaming scenario, client sends a stream of requests and server responds with a single response.
        /// </summary>
        /// <param name="call">The call defintion.</param>
        /// <returns>An awaitable call object providing access to the response.</returns>
        /// <typeparam name="TRequest">Type of request messages.</typeparam>
        /// <typeparam name="TResponse">The of response message.</typeparam>
        public static AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            var resultTask = asyncCall.ClientStreamingCallAsync();
            var requestStream = new ClientRequestStream<TRequest, TResponse>(asyncCall);
            return new AsyncClientStreamingCall<TRequest, TResponse>(requestStream, resultTask, asyncCall.ResponseHeadersAsync, asyncCall.GetStatus, asyncCall.GetTrailers, asyncCall.Cancel);
        }

        /// <summary>
        /// Invokes a duplex streaming call asynchronously.
        /// In duplex streaming scenario, client sends a stream of requests and server responds with a stream of responses.
        /// The response stream is completely independent and both side can be sending messages at the same time.
        /// </summary>
        /// <returns>A call object providing access to the asynchronous request and response streams.</returns>
        /// <param name="call">The call definition.</param>
        /// <typeparam name="TRequest">Type of request messages.</typeparam>
        /// <typeparam name="TResponse">Type of reponse messages.</typeparam>
        public static AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ClientResponseStream<TRequest, TResponse>(asyncCall);
            return new AsyncDuplexStreamingCall<TRequest, TResponse>(requestStream, responseStream, asyncCall.ResponseHeadersAsync, asyncCall.GetStatus, asyncCall.GetTrailers, asyncCall.Cancel);
        }
    }
}
