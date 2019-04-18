#region Copyright notice and license

// Copyright 2018 gRPC authors.
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
using Grpc.Core.Utils;

namespace Grpc.Core.Interceptors
{
    /// <summary>
    /// Decorates an underlying <see cref="Grpc.Core.CallInvoker" /> to
    /// intercept calls through a given interceptor.
    /// </summary>
    internal class InterceptingCallInvoker : CallInvoker
    {
        readonly CallInvoker invoker;
        readonly Interceptor interceptor;

        /// <summary>
        /// Creates a new instance of <see cref="Grpc.Core.Interceptors.InterceptingCallInvoker" />
        /// with the given underlying invoker and interceptor instances.
        /// </summary>
        public InterceptingCallInvoker(CallInvoker invoker, Interceptor interceptor)
        {
            this.invoker = GrpcPreconditions.CheckNotNull(invoker, nameof(invoker));
            this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));
        }

        /// <summary>
        /// Intercepts a simple blocking call with the registered interceptor.
        /// </summary>
        public override TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            return interceptor.BlockingUnaryCall(
                request,
                new ClientInterceptorContext<TRequest, TResponse>(method, host, options),
                (req, ctx) => invoker.BlockingUnaryCall(ctx.Method, ctx.Host, ctx.Options, req));
        }

        /// <summary>
        /// Intercepts a simple asynchronous call with the registered interceptor.
        /// </summary>
        public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            return interceptor.AsyncUnaryCall(
                request,
                new ClientInterceptorContext<TRequest, TResponse>(method, host, options),
                (req, ctx) => invoker.AsyncUnaryCall(ctx.Method, ctx.Host, ctx.Options, req));
        }

        /// <summary>
        /// Intercepts an asynchronous server streaming call with the registered interceptor.
        /// </summary>
        public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
        {
            return interceptor.AsyncServerStreamingCall(
                request,
                new ClientInterceptorContext<TRequest, TResponse>(method, host, options),
                (req, ctx) => invoker.AsyncServerStreamingCall(ctx.Method, ctx.Host, ctx.Options, req));
        }

        /// <summary>
        /// Intercepts an asynchronous client streaming call with the registered interceptor.
        /// </summary>
        public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            return interceptor.AsyncClientStreamingCall(
                new ClientInterceptorContext<TRequest, TResponse>(method, host, options),
                ctx => invoker.AsyncClientStreamingCall(ctx.Method, ctx.Host, ctx.Options));
        }

        /// <summary>
        /// Intercepts an asynchronous duplex streaming call with the registered interceptor.
        /// </summary>
        public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            return interceptor.AsyncDuplexStreamingCall(
                new ClientInterceptorContext<TRequest, TResponse>(method, host, options),
                ctx => invoker.AsyncDuplexStreamingCall(ctx.Method, ctx.Host, ctx.Options));
        }
    }
}
