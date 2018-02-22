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
using System.Reflection;
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core.Interceptors
{
    /// <summary>
    /// Serves as the base class for gRPC interceptors.
    /// This is an EXPERIMENTAL API.
    /// </summary>
    public abstract class Interceptor
    {
        /// <summary>
        /// Represents a continuation for intercepting simple blocking invocations.
        /// A delegate of this type is passed to the BlockingUnaryCall method
        /// when an outgoing invocation is being intercepted and calling the
        /// delegate will invoke the next interceptor in the chain, or the underlying
        /// call invoker if called from the last interceptor. The interceptor is
        /// allowed to call it zero, one, or multiple times, passing it the appropriate
        /// context and request values as it sees fit.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this invocation.</typeparam>
        /// <typeparam name="TResponse">Response message type for this invocation.</typeparam>
        /// <param name="request">The request value to continue the invocation with.</param>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// instance to pass to the next step in the invocation process.
        /// </param>
        /// <returns>
        /// The response value of the invocation to return to the caller.
        /// The interceptor can choose to return the return value of the
        /// continuation delegate or an arbitrary value as it sees fit.
        /// </returns>
        public delegate TResponse BlockingUnaryCallContinuation<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context)
            where TRequest : class
            where TResponse : class;

        /// <summary>
        /// Represents a continuation for intercepting simple asynchronous invocations.
        /// A delegate of this type is passed to the AsyncUnaryCall method
        /// when an outgoing invocation is being intercepted and calling the
        /// delegate will invoke the next interceptor in the chain, or the underlying
        /// call invoker if called from the last interceptor. The interceptor is
        /// allowed to call it zero, one, or multiple times, passing it the appropriate
        /// request value and context as it sees fit.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this invocation.</typeparam>
        /// <typeparam name="TResponse">Response message type for this invocation.</typeparam>
        /// <param name="request">The request value to continue the invocation with.</param>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// instance to pass to the next step in the invocation process.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncUnaryCall{TResponse}" />
        /// representing an asynchronous invocation of a unary RPC.
        /// The interceptor can choose to return the same object returned from
        /// the continuation delegate or an arbitrarily constructed instance as it sees fit.
        /// </returns>
        public delegate AsyncUnaryCall<TResponse> AsyncUnaryCallContinuation<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context)
            where TRequest : class
            where TResponse : class;

        /// <summary>
        /// Represents a continuation for intercepting asynchronous server-streaming invocations.
        /// A delegate of this type is passed to the AsyncServerStreamingCall method
        /// when an outgoing invocation is being intercepted and calling the
        /// delegate will invoke the next interceptor in the chain, or the underlying
        /// call invoker if called from the last interceptor. The interceptor is
        /// allowed to call it zero, one, or multiple times, passing it the appropriate
        /// request value and context as it sees fit.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this invocation.</typeparam>
        /// <typeparam name="TResponse">Response message type for this invocation.</typeparam>
        /// <param name="request">The request value to continue the invocation with.</param>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// instance to pass to the next step in the invocation process.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncServerStreamingCall{TResponse}" />
        /// representing an asynchronous invocation of a server-streaming RPC.
        /// The interceptor can choose to return the same object returned from
        /// the continuation delegate or an arbitrarily constructed instance as it sees fit.
        /// </returns>
        public delegate AsyncServerStreamingCall<TResponse> AsyncServerStreamingCallContinuation<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context)
            where TRequest : class
            where TResponse : class;

        /// <summary>
        /// Represents a continuation for intercepting asynchronous client-streaming invocations.
        /// A delegate of this type is passed to the AsyncClientStreamingCall method
        /// when an outgoing invocation is being intercepted and calling the
        /// delegate will invoke the next interceptor in the chain, or the underlying
        /// call invoker if called from the last interceptor. The interceptor is
        /// allowed to call it zero, one, or multiple times, passing it the appropriate
        /// request value and context as it sees fit.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this invocation.</typeparam>
        /// <typeparam name="TResponse">Response message type for this invocation.</typeparam>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// instance to pass to the next step in the invocation process.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncClientStreamingCall{TRequest, TResponse}" />
        /// representing an asynchronous invocation of a client-streaming RPC.
        /// The interceptor can choose to return the same object returned from
        /// the continuation delegate or an arbitrarily constructed instance as it sees fit.
        /// </returns>
        public delegate AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCallContinuation<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context)
            where TRequest : class
            where TResponse : class;

        /// <summary>
        /// Represents a continuation for intercepting asynchronous duplex invocations.
        /// A delegate of this type is passed to the AsyncDuplexStreamingCall method
        /// when an outgoing invocation is being intercepted and calling the
        /// delegate will invoke the next interceptor in the chain, or the underlying
        /// call invoker if called from the last interceptor. The interceptor is
        /// allowed to call it zero, one, or multiple times, passing it the appropriate
        /// request value and context as it sees fit.
        /// </summary>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// instance to pass to the next step in the invocation process.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncDuplexStreamingCall{TRequest, TResponse}" />
        /// representing an asynchronous invocation of a duplex-streaming RPC.
        /// The interceptor can choose to return the same object returned from
        /// the continuation delegate or an arbitrarily constructed instance as it sees fit.
        /// </returns>
        public delegate AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCallContinuation<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context)
            where TRequest : class
            where TResponse : class;

        /// <summary>
        /// Intercepts a blocking invocation of a simple remote call.
        /// </summary>
        /// <param name="request">The request message of the invocation.</param>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// associated with the current invocation.
        /// </param>
        /// <param name="continuation">
        /// The callback that continues the invocation process.
        /// This can be invoked zero or more times by the interceptor.
        /// The interceptor can invoke the continuation passing the given
        /// request value and context arguments, or substitute them as it sees fit.
        /// </param>
        /// <returns>
        /// The response message of the current invocation.
        /// The interceptor can simply return the return value of the
        /// continuation delegate passed to it intact, or an arbitrary
        /// value as it sees fit.
        /// </returns>
        public virtual TResponse BlockingUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, BlockingUnaryCallContinuation<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(request, context);
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a simple remote call.
        /// </summary>
        /// <param name="request">The request message of the invocation.</param>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// associated with the current invocation.
        /// </param>
        /// <param name="continuation">
        /// The callback that continues the invocation process.
        /// This can be invoked zero or more times by the interceptor.
        /// The interceptor can invoke the continuation passing the given
        /// request value and context arguments, or substitute them as it sees fit.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncUnaryCall{TResponse}" />
        /// representing an asynchronous unary invocation.
        /// The interceptor can simply return the return value of the
        /// continuation delegate passed to it intact, or construct its
        /// own substitute as it sees fit.
        /// </returns>
        public virtual AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncUnaryCallContinuation<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(request, context);
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a streaming remote call.
        /// </summary>
        /// <param name="request">The request message of the invocation.</param>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// associated with the current invocation.
        /// </param>
        /// <param name="continuation">
        /// The callback that continues the invocation process.
        /// This can be invoked zero or more times by the interceptor.
        /// The interceptor can invoke the continuation passing the given
        /// request value and context arguments, or substitute them as it sees fit.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncServerStreamingCall{TResponse}" />
        /// representing an asynchronous server-streaming invocation.
        /// The interceptor can simply return the return value of the
        /// continuation delegate passed to it intact, or construct its
        /// own substitute as it sees fit.
        /// </returns>
        public virtual AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncServerStreamingCallContinuation<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(request, context);
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a client streaming call.
        /// </summary>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// associated with the current invocation.
        /// </param>
        /// <param name="continuation">
        /// The callback that continues the invocation process.
        /// This can be invoked zero or more times by the interceptor.
        /// The interceptor can invoke the continuation passing the given
        /// context argument, or substitute as it sees fit.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncClientStreamingCall{TRequest, TResponse}" />
        /// representing an asynchronous client-streaming invocation.
        /// The interceptor can simply return the return value of the
        /// continuation delegate passed to it intact, or construct its
        /// own substitute as it sees fit.
        /// </returns>
        public virtual AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncClientStreamingCallContinuation<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(context);
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a duplex streaming call.
        /// </summary>
        /// <param name="context">
        /// The <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}"/>
        /// associated with the current invocation.
        /// </param>
        /// <param name="continuation">
        /// The callback that continues the invocation process.
        /// This can be invoked zero or more times by the interceptor.
        /// The interceptor can invoke the continuation passing the given
        /// context argument, or substitute as it sees fit.
        /// </param>
        /// <returns>
        /// An instance of <see cref="Grpc.Core.AsyncDuplexStreamingCall{TRequest, TResponse}" />
        /// representing an asynchronous duplex-streaming invocation.
        /// The interceptor can simply return the return value of the
        /// continuation delegate passed to it intact, or construct its
        /// own substitute as it sees fit.
        /// </returns>
        public virtual AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncDuplexStreamingCallContinuation<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(context);
        }

        /// <summary>
        /// Server-side handler for intercepting and incoming unary call.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        /// <param name="request">The request value of the incoming invocation.</param>
        /// <param name="context">
        /// An instance of <see cref="Grpc.Core.ServerCallContext" /> representing
        /// the context of the invocation.
        /// </param>
        /// <param name="continuation">
        /// A delegate that asynchronously proceeds with the invocation, calling
        /// the next interceptor in the chain, or the service request handler,
        /// in case of the last interceptor and return the response value of
        /// the RPC. The interceptor can choose to call it zero or more times
        /// at its discretion.
        /// </param>
        /// <returns>
        /// A future representing the response value of the RPC. The interceptor
        /// can simply return the return value from the continuation intact,
        /// or an arbitrary response value as it sees fit.
        /// </returns>
        public virtual Task<TResponse> UnaryServerHandler<TRequest, TResponse>(TRequest request, ServerCallContext context, UnaryServerMethod<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(request, context);
        }

        /// <summary>
        /// Server-side handler for intercepting client streaming call.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        /// <param name="requestStream">The request stream of the incoming invocation.</param>
        /// <param name="context">
        /// An instance of <see cref="Grpc.Core.ServerCallContext" /> representing
        /// the context of the invocation.
        /// </param>
        /// <param name="continuation">
        /// A delegate that asynchronously proceeds with the invocation, calling
        /// the next interceptor in the chain, or the service request handler,
        /// in case of the last interceptor and return the response value of
        /// the RPC. The interceptor can choose to call it zero or more times
        /// at its discretion.
        /// </param>
        /// <returns>
        /// A future representing the response value of the RPC. The interceptor
        /// can simply return the return value from the continuation intact,
        /// or an arbitrary response value as it sees fit. The interceptor has
        /// the ability to wrap or substitute the request stream when calling
        /// the continuation.
        /// </returns>
        public virtual Task<TResponse> ClientStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream, ServerCallContext context, ClientStreamingServerMethod<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(requestStream, context);
        }

        /// <summary>
        /// Server-side handler for intercepting server streaming call.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        /// <param name="request">The request value of the incoming invocation.</param>
        /// <param name="responseStream">The response stream of the incoming invocation.</param>
        /// <param name="context">
        /// An instance of <see cref="Grpc.Core.ServerCallContext" /> representing
        /// the context of the invocation.
        /// </param>
        /// <param name="continuation">
        /// A delegate that asynchronously proceeds with the invocation, calling
        /// the next interceptor in the chain, or the service request handler,
        /// in case of the last interceptor and the interceptor can choose to
        /// call it zero or more times at its discretion. The interceptor has
        /// the ability to wrap or substitute the request value and the response stream
        /// when calling the continuation.
        /// </param>
        public virtual Task ServerStreamingServerHandler<TRequest, TResponse>(TRequest request, IServerStreamWriter<TResponse> responseStream, ServerCallContext context, ServerStreamingServerMethod<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(request, responseStream, context);
        }

        /// <summary>
        /// Server-side handler for intercepting bidirectional streaming calls.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        /// <param name="requestStream">The request stream of the incoming invocation.</param>
        /// <param name="responseStream">The response stream of the incoming invocation.</param>
        /// <param name="context">
        /// An instance of <see cref="Grpc.Core.ServerCallContext" /> representing
        /// the context of the invocation.
        /// </param>
        /// <param name="continuation">
        /// A delegate that asynchronously proceeds with the invocation, calling
        /// the next interceptor in the chain, or the service request handler,
        /// in case of the last interceptor and the interceptor can choose to
        /// call it zero or more times at its discretion. The interceptor has
        /// the ability to wrap or substitute the request and response streams
        /// when calling the continuation.
        /// </param>
        public virtual Task DuplexStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream, IServerStreamWriter<TResponse> responseStream, ServerCallContext context, DuplexStreamingServerMethod<TRequest, TResponse> continuation)
            where TRequest : class
            where TResponse : class
        {
            return continuation(requestStream, responseStream, context);
        }
    }
}
