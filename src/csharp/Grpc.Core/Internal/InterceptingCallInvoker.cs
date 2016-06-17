#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
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
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Decorates an underlying <c>CallInvoker</c> to intercept call invocations.
    /// </summary>
    internal class InterceptingCallInvoker : CallInvoker
    {
        readonly CallInvoker callInvoker;
        readonly Func<string, string> hostInterceptor;
        readonly Func<CallOptions, CallOptions> callOptionsInterceptor;

        /// <summary>
        /// Initializes a new instance of the <see cref="Grpc.Core.InterceptingCallInvoker"/> class.
        /// </summary>
        public InterceptingCallInvoker(CallInvoker callInvoker,
            Func<string, string> hostInterceptor = null,
            Func<CallOptions, CallOptions> callOptionsInterceptor = null)
        {
            this.callInvoker = GrpcPreconditions.CheckNotNull(callInvoker);
            this.hostInterceptor = hostInterceptor;
            this.callOptionsInterceptor = callOptionsInterceptor;
        }

        /// <summary>
        /// Intercepts a unary call.
        /// </summary>
        public override TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            host = InterceptHost(host);
            options = InterceptCallOptions(options);
            return callInvoker.BlockingUnaryCall(method, host, options, request);
        }

        /// <summary>
        /// Invokes a simple remote call asynchronously.
        /// </summary>
        public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            host = InterceptHost(host);
            options = InterceptCallOptions(options);
            return callInvoker.AsyncUnaryCall(method, host, options, request);
        }

        /// <summary>
        /// Invokes a server streaming call asynchronously.
        /// In server streaming scenario, client sends on request and server responds with a stream of responses.
        /// </summary>
        public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            host = InterceptHost(host);
            options = InterceptCallOptions(options);
            return callInvoker.AsyncServerStreamingCall(method, host, options, request);
        }

        /// <summary>
        /// Invokes a client streaming call asynchronously.
        /// In client streaming scenario, client sends a stream of requests and server responds with a single response.
        /// </summary>
        public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            host = InterceptHost(host);
            options = InterceptCallOptions(options);
            return callInvoker.AsyncClientStreamingCall(method, host, options);
        }

        /// <summary>
        /// Invokes a duplex streaming call asynchronously.
        /// In duplex streaming scenario, client sends a stream of requests and server responds with a stream of responses.
        /// The response stream is completely independent and both side can be sending messages at the same time.
        /// </summary>
        public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            host = InterceptHost(host);
            options = InterceptCallOptions(options);
            return callInvoker.AsyncDuplexStreamingCall(method, host, options);
        }

        private string InterceptHost(string host)
        {
            if (hostInterceptor == null)
            {
                return host;
            }
            return hostInterceptor(host);
        }

        private CallOptions InterceptCallOptions(CallOptions options)
        {
            if (callOptionsInterceptor == null)
            {
                return options;
            }
            return callOptionsInterceptor(options);
        }
    }
}
