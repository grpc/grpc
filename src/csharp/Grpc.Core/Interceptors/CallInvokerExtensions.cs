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
            GrpcPreconditions.CheckNotNull(invoker, nameof(invoker));
            GrpcPreconditions.CheckNotNull(interceptors, nameof(interceptors));

            foreach (var interceptor in interceptors.Reverse())
            {
                invoker = Intercept(invoker, interceptor);
            }

            return invoker;
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
        /// Multiple interceptors can be added on top of each other by
        /// building a chain like "invoker.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static CallInvoker Intercept(this CallInvoker invoker, Func<Metadata, Metadata> interceptor)
        {
            return new InterceptingCallInvoker(invoker, new MetadataInterceptor(interceptor));
        }

        private class MetadataInterceptor : Interceptor
        {
            readonly Func<Metadata, Metadata> interceptor;

            /// <summary>
            /// Creates a new instance of MetadataInterceptor given the specified interceptor function.
            /// </summary>
            public MetadataInterceptor(Func<Metadata, Metadata> interceptor)
            {
                this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));
            }

            private ClientInterceptorContext<TRequest, TResponse> GetNewContext<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context)
                where TRequest : class
                where TResponse : class
            {
                var metadata = context.Options.Headers ?? new Metadata();
                return new ClientInterceptorContext<TRequest, TResponse>(context.Method, context.Host, context.Options.WithHeaders(interceptor(metadata)));
            }

            public override TResponse BlockingUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, BlockingUnaryCallContinuation<TRequest, TResponse> continuation)
            {
                return continuation(request, GetNewContext(context));
            }

            public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncUnaryCallContinuation<TRequest, TResponse> continuation)
            {
                return continuation(request, GetNewContext(context));
            }

            public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncServerStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                return continuation(request, GetNewContext(context));
            }

            public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncClientStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                return continuation(GetNewContext(context));
            }

            public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncDuplexStreamingCallContinuation<TRequest, TResponse> continuation)
            {
                return continuation(GetNewContext(context));
            }
        }
    }
}
