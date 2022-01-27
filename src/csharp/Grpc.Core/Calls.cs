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
        /// <param name="call">The call definition.</param>
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
        /// <param name="call">The call definition.</param>
        /// <param name="req">Request message.</param>
        /// <typeparam name="TRequest">Type of request message.</typeparam>
        /// <typeparam name="TResponse">The of response message.</typeparam>
        public static AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call, TRequest req)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            var asyncResult = asyncCall.UnaryCallAsync(req);
            return new AsyncUnaryCall<TResponse>(asyncResult,
                Callbacks<TRequest, TResponse>.GetHeaders, Callbacks<TRequest, TResponse>.GetStatus,
                Callbacks<TRequest, TResponse>.GetTrailers, Callbacks<TRequest, TResponse>.Cancel,
                asyncCall);
        }

        /// <summary>
        /// Invokes a server streaming call asynchronously.
        /// In server streaming scenario, client sends on request and server responds with a stream of responses.
        /// </summary>
        /// <returns>A call object providing access to the asynchronous response stream.</returns>
        /// <param name="call">The call definition.</param>
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
            return new AsyncServerStreamingCall<TResponse>(responseStream,
                Callbacks<TRequest, TResponse>.GetHeaders, Callbacks<TRequest, TResponse>.GetStatus,
                Callbacks<TRequest, TResponse>.GetTrailers, Callbacks<TRequest, TResponse>.Cancel,
                asyncCall);
        }

        /// <summary>
        /// Invokes a client streaming call asynchronously.
        /// In client streaming scenario, client sends a stream of requests and server responds with a single response.
        /// </summary>
        /// <param name="call">The call definition.</param>
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
            return new AsyncClientStreamingCall<TRequest, TResponse>(requestStream, resultTask,
                Callbacks<TRequest, TResponse>.GetHeaders, Callbacks<TRequest, TResponse>.GetStatus,
                Callbacks<TRequest, TResponse>.GetTrailers, Callbacks<TRequest, TResponse>.Cancel,
                asyncCall);
        }

        /// <summary>
        /// Invokes a duplex streaming call asynchronously.
        /// In duplex streaming scenario, client sends a stream of requests and server responds with a stream of responses.
        /// The response stream is completely independent and both side can be sending messages at the same time.
        /// </summary>
        /// <returns>A call object providing access to the asynchronous request and response streams.</returns>
        /// <param name="call">The call definition.</param>
        /// <typeparam name="TRequest">Type of request messages.</typeparam>
        /// <typeparam name="TResponse">Type of responsemessages.</typeparam>
        public static AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(CallInvocationDetails<TRequest, TResponse> call)
            where TRequest : class
            where TResponse : class
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call);
            asyncCall.StartDuplexStreamingCall();
            var requestStream = new ClientRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ClientResponseStream<TRequest, TResponse>(asyncCall);
            return new AsyncDuplexStreamingCall<TRequest, TResponse>(requestStream, responseStream,
                Callbacks<TRequest, TResponse>.GetHeaders, Callbacks<TRequest, TResponse>.GetStatus,
                Callbacks<TRequest, TResponse>.GetTrailers, Callbacks<TRequest, TResponse>.Cancel,
                asyncCall);
        }

        private static class Callbacks<TRequest, TResponse>
        {
            internal static readonly Func<object, Task<Metadata>> GetHeaders = state => ((AsyncCall<TRequest, TResponse>)state).ResponseHeadersAsync;
            internal static readonly Func<object, Status> GetStatus = state => ((AsyncCall<TRequest, TResponse>)state).GetStatus();
            internal static readonly Func<object, Metadata> GetTrailers = state => ((AsyncCall<TRequest, TResponse>)state).GetTrailers();
            internal static readonly Action<object> Cancel = state => ((AsyncCall<TRequest, TResponse>)state).Cancel();
        }
    }
}
