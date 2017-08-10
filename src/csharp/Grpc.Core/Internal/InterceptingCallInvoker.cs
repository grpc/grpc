#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal class InterceptingCallInvoker : CallInvoker
    {
	readonly IInterceptor interceptor;
        readonly CallInvoker callInvoker;

        /// <summary>
        /// Initializes a new instance of the <see cref="Grpc.Core.Internal.InterceptingCallInvoker"/> class.
        /// </summary>
        public InterceptingCallInvoker(IInterceptor interceptor, CallInvoker callInvoker)
        {
		this.interceptor = interceptor;
		this.callInvoker = callInvoker;
        }

        /// <summary>
        /// Intercepts a unary call.
        /// </summary>
        public override TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            return interceptor.BlockingUnaryCall(method, host, options, request, callInvoker.BlockingUnaryCall);
        }

        /// <summary>
        /// Invokes a simple remote call asynchronously.
        /// </summary>
        public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            return interceptor.AsyncUnaryCall(method, host, options, request, callInvoker.AsyncUnaryCall);
        }

        /// <summary>
        /// Invokes a server streaming call asynchronously.
        /// In server streaming scenario, client sends on request and server responds with a stream of responses.
        /// </summary>
        public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            return interceptor.AsyncServerStreamingCall(method, host, options, request, callInvoker.AsyncServerStreamingCall);
        }

        /// <summary>
        /// Invokes a client streaming call asynchronously.
        /// In client streaming scenario, client sends a stream of requests and server responds with a single response.
        /// </summary>
        public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            return interceptor.AsyncClientStreamingCall(method, host, options, callInvoker.AsyncClientStreamingCall);
        }

        /// <summary>
        /// Invokes a duplex streaming call asynchronously.
        /// In duplex streaming scenario, client sends a stream of requests and server responds with a stream of responses.
        /// The response stream is completely independent and both side can be sending messages at the same time.
        /// </summary>
        public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            return interceptor.AsyncDuplexStreamingCall(method, host, options, callInvoker.AsyncDuplexStreamingCall);
        }
    }
}
