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
using System.Linq;
using Grpc.Core.Utils;

namespace Grpc.Core.Interceptors
{
    /// <summary>
    /// Extends the CallInvoker class to provide the interceptor facility on the client side.
    /// This is an EXPERIMENTAL API.
    /// </summary>
    public static class CallInvokerExtensions
    {
        /// <summary>
        /// Decorates an underlying <see cref="Grpc.Core.CallInvoker" /> to
        /// intercept calls through a given interceptor.
        /// </summary>
        private class InterceptingCallInvoker : CallInvoker
        {
            readonly CallInvoker invoker;
            readonly Interceptor interceptor;

            /// <summary>
            /// Creates a new instance of <see cref="Grpc.Core.Interceptors.CallInvokerExtensions.InterceptingCallInvoker" />
            /// with the given underlying invoker and interceptor instances.
            /// </summary>
            public InterceptingCallInvoker(CallInvoker invoker, Interceptor interceptor)
            {
                this.invoker = GrpcPreconditions.CheckNotNull(invoker, "invoker");
                this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, "interceptor");
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

        private class MetadataInterceptor : GenericInterceptor
        {
            readonly Func<Metadata, Metadata> interceptor;

            /// <summary>
            /// Creates a new instance of MetadataInterceptor given the specified interceptor function.
            /// </summary>
            public MetadataInterceptor(Func<Metadata, Metadata> interceptor)
            {
                this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, "interceptor");
            }

            protected override ClientCallHooks<TRequest, TResponse> InterceptCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, bool clientStreaming, bool serverStreaming, TRequest request)
            {
                var metadata = context.Options.Headers ?? new Metadata();
                return new ClientCallHooks<TRequest, TResponse>
                {
                    ContextOverride = new ClientInterceptorContext<TRequest, TResponse>(context.Method, context.Host, context.Options.WithHeaders(interceptor(metadata))),
                };
            }
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.CallInvoker" /> instance that intercepts
        /// the invoker with the given interceptor.
        /// </summary>
        /// <param name="invoker">The underlying invoker to intercept.</param>
        /// <param name="interceptor">
        /// An interceptor delegate that takes the request metadata to be sent with an outgoing call
        /// and returns a <see cref="Grpc.Core.Metadata" /> instance that will replace the existing
        /// invocation metadata.
        /// </param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "invoker.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted CallInvoker, effectively
        /// building a chain like "invoker.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this CallInvoker invoker, Func<Metadata, Metadata> interceptor)
        {
            return new InterceptingCallInvoker(invoker, new MetadataInterceptor(interceptor));
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.CallInvoker" /> instance that intercepts
        /// the invoker with the given interceptor.
        /// </summary>
        /// <param name="invoker">The underlying invoker to intercept.</param>
        /// <param name="interceptor">The interceptor to intercept calls to the invoker with.</param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "invoker.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted CallInvoker, effectively
        /// building a chain like "invoker.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this CallInvoker invoker, Interceptor interceptor)
        {
            return new InterceptingCallInvoker(invoker, interceptor);
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.CallInvoker" /> instance that intercepts
        /// the invoker with the given interceptors.
        /// </summary>
        /// <param name="invoker">The channel to intercept.</param>
        /// <param name="interceptors">
        /// An array of interceptors to intercept the calls to the invoker with.
        /// Control is passed to the interceptors in the order specified.
        /// </param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "invoker.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted CallInvoker, effectively
        /// building a chain like "invoker.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this CallInvoker invoker, params Interceptor[] interceptors)
        {
            GrpcPreconditions.CheckNotNull(invoker, "invoker");
            GrpcPreconditions.CheckNotNull(interceptors, "interceptors");

            foreach (var interceptor in interceptors.Reverse())
            {
                invoker = Intercept(invoker, interceptor);
            }

            return invoker;
        }
    }
}
