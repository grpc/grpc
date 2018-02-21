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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core.Interceptors
{
    /// <summary>
    /// Provides a base class for generic interceptor implementations that raises
    /// events and hooks to control the RPC lifecycle.
    /// </summary>
    internal abstract class GenericInterceptor : Interceptor
    {
        /// <summary>
        /// Provides hooks through which an invocation should be intercepted.
        /// </summary>
        public sealed class ClientCallHooks<TRequest, TResponse>
            where TRequest : class
            where TResponse : class
        {
            internal ClientCallHooks<TRequest, TResponse> Freeze()
            {
                return (ClientCallHooks<TRequest, TResponse>)MemberwiseClone();
            }
            /// <summary>
            /// Override the context for the outgoing invocation.
            /// </summary>
            public ClientInterceptorContext<TRequest, TResponse>? ContextOverride { get; set; }
            /// <summary>
            /// Override the request for the outgoing invocation for non-client-streaming invocations.
            /// </summary>
            public TRequest UnaryRequestOverride { get; set; }
            /// <summary>
            /// Delegate that intercepts a response from a non-server-streaming invocation and optionally overrides it.
            /// </summary>
            public Func<TResponse, TResponse> OnUnaryResponse { get; set; }
            /// <summary>
            /// Delegate that intercepts each request message for a client-streaming invocation and optionally overrides each message.
            /// </summary>
            public Func<TRequest, TRequest> OnRequestMessage { get; set; }
            /// <summary>
            /// Delegate that intercepts each response message for a server-streaming invocation and optionally overrides each message.
            /// </summary>
            public Func<TResponse, TResponse> OnResponseMessage { get; set; }
            /// <summary>
            /// Callback that gets invoked when response stream is finished.
            /// </summary>
            public Action OnResponseStreamEnd { get; set; }
            /// <summary>
            /// Callback that gets invoked when request stream is finished.
            /// </summary>
            public Action OnRequestStreamEnd { get; set; }
        }

        /// <summary>
        /// Intercepts an outgoing call from the client side.
        /// Derived classes that intend to intercept outgoing invocations from the client side should
        /// override this and return the appropriate hooks in the form of a ClientCallHooks instance.
        /// </summary>
        /// <param name="context">The context of the outgoing invocation.</param>
        /// <param name="clientStreaming">True if the invocation is client-streaming.</param>
        /// <param name="serverStreaming">True if the invocation is server-streaming.</param>
        /// <param name="request">The request message for client-unary invocations, null otherwise.</param>
        /// <typeparam name="TRequest">Request message type for the current invocation.</typeparam>
        /// <typeparam name="TResponse">Response message type for the current invocation.</typeparam>
        /// <returns>
        /// The derived class should return an instance of ClientCallHooks to control the trajectory
        /// as they see fit, or null if it does not intend to pursue the invocation any further.
        /// </returns>
        protected virtual ClientCallHooks<TRequest, TResponse> InterceptCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, bool clientStreaming, bool serverStreaming, TRequest request)
            where TRequest : class
            where TResponse : class
        {
            return null;
        }

        /// <summary>
        /// Provides hooks through which a server-side handler should be intercepted.
        /// </summary>
        public sealed class ServerCallHooks<TRequest, TResponse>
            where TRequest : class
            where TResponse : class
        {
            internal ServerCallHooks<TRequest, TResponse> Freeze()
            {
                return (ServerCallHooks<TRequest, TResponse>)MemberwiseClone();
            }
            /// <summary>
            /// Override the request for the outgoing invocation for non-client-streaming invocations.
            /// </summary>
            public TRequest UnaryRequestOverride { get; set; }
            /// <summary>
            /// Delegate that intercepts a response from a non-server-streaming invocation and optionally overrides it.
            /// </summary>
            public Func<TResponse, TResponse> OnUnaryResponse { get; set; }
            /// <summary>
            /// Delegate that intercepts each request message for a client-streaming invocation and optionally overrides each message.
            /// </summary>
            public Func<TRequest, TRequest> OnRequestMessage { get; set; }
            /// <summary>
            /// Delegate that intercepts each response message for a server-streaming invocation and optionally overrides each message.
            /// </summary>
            public Func<TResponse, TResponse> OnResponseMessage { get; set; }
            /// <summary>
            /// Callback that gets invoked when handler is finished executing.
            /// </summary>
            public Action OnHandlerEnd { get; set; }
            /// <summary>
            /// Callback that gets invoked when request stream is finished.
            /// </summary>
            public Action OnRequestStreamEnd { get; set; }
        }

        /// <summary>
        /// Intercepts an incoming service handler invocation on the server side.
        /// Derived classes that intend to intercept incoming handlers on the server side should
        /// override this and return the appropriate hooks in the form of a ServerCallHooks instance.
        /// </summary>
        /// <param name="context">The context of the incoming invocation.</param>
        /// <param name="clientStreaming">True if the invocation is client-streaming.</param>
        /// <param name="serverStreaming">True if the invocation is server-streaming.</param>
        /// <param name="request">The request message for client-unary invocations, null otherwise.</param>
        /// <typeparam name="TRequest">Request message type for the current invocation.</typeparam>
        /// <typeparam name="TResponse">Response message type for the current invocation.</typeparam>
        /// <returns>
        /// The derived class should return an instance of ServerCallHooks to control the trajectory
        /// as they see fit, or null if it does not intend to pursue the invocation any further.
        /// </returns>
        protected virtual Task<ServerCallHooks<TRequest, TResponse>> InterceptHandler<TRequest, TResponse>(ServerCallContext context, bool clientStreaming, bool serverStreaming, TRequest request)
            where TRequest : class
            where TResponse : class
        {
            return Task.FromResult<ServerCallHooks<TRequest, TResponse>>(null);
        }

        /// <summary>
        /// Intercepts a blocking invocation of a simple remote call and dispatches the events accordingly.
        /// </summary>
        public override TResponse BlockingUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, BlockingUnaryCallContinuation<TRequest, TResponse> continuation)
        {
            var hooks = InterceptCall(context, false, false, request)?.Freeze();
            context = hooks?.ContextOverride ?? context;
            request = hooks?.UnaryRequestOverride ?? request;
            var response = continuation(request, context);
            if (hooks?.OnUnaryResponse != null)
            {
                response = hooks.OnUnaryResponse(response);
            }
            return response;
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a simple remote call and dispatches the events accordingly.
        /// </summary>
        public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncUnaryCallContinuation<TRequest, TResponse> continuation)
        {
            var hooks = InterceptCall(context, false, false, request)?.Freeze();
            context = hooks?.ContextOverride ?? context;
            request = hooks?.UnaryRequestOverride ?? request;
            var response = continuation(request, context);
            if (hooks?.OnUnaryResponse != null)
            {
                response = new AsyncUnaryCall<TResponse>(response.ResponseAsync.ContinueWith(unaryResponse => hooks.OnUnaryResponse(unaryResponse.Result)),
                    response.ResponseHeadersAsync, response.GetStatus, response.GetTrailers, response.Dispose);
            }
            return response;
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a streaming remote call and dispatches the events accordingly.
        /// </summary>
        public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncServerStreamingCallContinuation<TRequest, TResponse> continuation)
        {
            var hooks = InterceptCall(context, false, true, request)?.Freeze();
            context = hooks?.ContextOverride ?? context;
            request = hooks?.UnaryRequestOverride ?? request;
            var response = continuation(request, context);
            if (hooks?.OnResponseMessage != null || hooks?.OnResponseStreamEnd != null)
            {
                response = new AsyncServerStreamingCall<TResponse>(
                    new WrappedAsyncStreamReader<TResponse>(response.ResponseStream, hooks.OnResponseMessage, hooks.OnResponseStreamEnd),
                    response.ResponseHeadersAsync, response.GetStatus, response.GetTrailers, response.Dispose);
            }
            return response;
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a client streaming call and dispatches the events accordingly.
        /// </summary>
        public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncClientStreamingCallContinuation<TRequest, TResponse> continuation)
        {
            var hooks = InterceptCall(context, true, false, null)?.Freeze();
            context = hooks?.ContextOverride ?? context;
            var response = continuation(context);
            if (hooks?.OnRequestMessage != null || hooks?.OnResponseStreamEnd != null || hooks?.OnUnaryResponse != null)
            {
                var requestStream = response.RequestStream;
                if (hooks?.OnRequestMessage != null || hooks?.OnRequestStreamEnd != null)
                {
                    requestStream = new WrappedClientStreamWriter<TRequest>(response.RequestStream, hooks.OnRequestMessage, hooks.OnRequestStreamEnd);
                }
                var responseAsync = response.ResponseAsync;
                if (hooks?.OnUnaryResponse != null)
                {
                    responseAsync = response.ResponseAsync.ContinueWith(unaryResponse => hooks.OnUnaryResponse(unaryResponse.Result));
                }
                response = new AsyncClientStreamingCall<TRequest, TResponse>(requestStream, responseAsync, response.ResponseHeadersAsync, response.GetStatus, response.GetTrailers, response.Dispose);
            }
            return response;
        }

        /// <summary>
        /// Intercepts an asynchronous invocation of a duplex streaming call and dispatches the events accordingly.
        /// </summary>
        public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncDuplexStreamingCallContinuation<TRequest, TResponse> continuation)
        {
            var hooks = InterceptCall(context, true, true, null)?.Freeze();
            context = hooks?.ContextOverride ?? context;
            var response = continuation(context);
            if (hooks?.OnRequestMessage != null || hooks?.OnRequestStreamEnd != null || hooks?.OnResponseMessage != null || hooks?.OnResponseStreamEnd != null)
            {
                var requestStream = response.RequestStream;
                if (hooks?.OnRequestMessage != null || hooks?.OnRequestStreamEnd != null)
                {
                    requestStream = new WrappedClientStreamWriter<TRequest>(response.RequestStream, hooks.OnRequestMessage, hooks.OnRequestStreamEnd);
                }
                var responseStream = response.ResponseStream;
                if (hooks?.OnResponseMessage != null || hooks?.OnResponseStreamEnd != null)
                {
                    responseStream = new WrappedAsyncStreamReader<TResponse>(response.ResponseStream, hooks.OnResponseMessage, hooks.OnResponseStreamEnd);
                }
                response = new AsyncDuplexStreamingCall<TRequest, TResponse>(requestStream, responseStream, response.ResponseHeadersAsync, response.GetStatus, response.GetTrailers, response.Dispose);
            }
            return response;
        }

        /// <summary>
        /// Server-side handler for intercepting unary calls.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        public override async Task<TResponse> UnaryServerHandler<TRequest, TResponse>(TRequest request, ServerCallContext context, UnaryServerMethod<TRequest, TResponse> continuation)
        {
            var hooks = (await InterceptHandler<TRequest, TResponse>(context, false, false, request))?.Freeze();
            request = hooks?.UnaryRequestOverride ?? request;
            var response = await continuation(request, context);
            if (hooks?.OnUnaryResponse != null)
            {
                response = hooks.OnUnaryResponse(response);
            }
            hooks?.OnHandlerEnd();
            return response;
        }

        /// <summary>
        /// Server-side handler for intercepting client streaming call.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        public override async Task<TResponse> ClientStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream, ServerCallContext context, ClientStreamingServerMethod<TRequest, TResponse> continuation)
        {
            var hooks = (await InterceptHandler<TRequest, TResponse>(context, true, false, null))?.Freeze();
            if (hooks?.OnRequestMessage != null || hooks?.OnRequestStreamEnd != null)
            {
                requestStream = new WrappedAsyncStreamReader<TRequest>(requestStream, hooks.OnRequestMessage, hooks.OnRequestStreamEnd);
            }
            var response = await continuation(requestStream, context);
            if (hooks?.OnUnaryResponse != null)
            {
                response = hooks.OnUnaryResponse(response);
            }
            hooks?.OnHandlerEnd();
            return response;
        }

        /// <summary>
        /// Server-side handler for intercepting server streaming calls.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        public override async Task ServerStreamingServerHandler<TRequest, TResponse>(TRequest request, IServerStreamWriter<TResponse> responseStream, ServerCallContext context, ServerStreamingServerMethod<TRequest, TResponse> continuation)
        {
            var hooks = (await InterceptHandler<TRequest, TResponse>(context, false, true, request))?.Freeze();
            request = hooks?.UnaryRequestOverride ?? request;
            if (hooks?.OnResponseMessage != null)
            {
                responseStream = new WrappedAsyncStreamWriter<TResponse>(responseStream, hooks.OnResponseMessage);
            }
            await continuation(request, responseStream, context);
            hooks?.OnHandlerEnd();
        }

        /// <summary>
        /// Server-side handler for intercepting bidi streaming calls.
        /// </summary>
        /// <typeparam name="TRequest">Request message type for this method.</typeparam>
        /// <typeparam name="TResponse">Response message type for this method.</typeparam>
        public override async Task DuplexStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream, IServerStreamWriter<TResponse> responseStream, ServerCallContext context, DuplexStreamingServerMethod<TRequest, TResponse> continuation)
        {
            var hooks = (await InterceptHandler<TRequest, TResponse>(context, true, true, null))?.Freeze();
            if (hooks?.OnRequestMessage != null || hooks?.OnRequestStreamEnd != null)
            {
                requestStream = new WrappedAsyncStreamReader<TRequest>(requestStream, hooks.OnRequestMessage, hooks.OnRequestStreamEnd);
            }
            if (hooks?.OnResponseMessage != null)
            {
                responseStream = new WrappedAsyncStreamWriter<TResponse>(responseStream, hooks.OnResponseMessage);
            }
            await continuation(requestStream, responseStream, context);
            hooks?.OnHandlerEnd();
        }

        private class WrappedAsyncStreamReader<T> : IAsyncStreamReader<T>
        {
            readonly IAsyncStreamReader<T> reader;
            readonly Func<T, T> onMessage;
            readonly Action onStreamEnd;
            public WrappedAsyncStreamReader(IAsyncStreamReader<T> reader, Func<T, T> onMessage, Action onStreamEnd)
            {
                this.reader = reader;
                this.onMessage = onMessage;
                this.onStreamEnd = onStreamEnd;
            }

            public void Dispose() => ((IDisposable)reader).Dispose();

            private T current;
            public T Current
            {
                get
                {
                    if (current == null)
                    {
                        throw new InvalidOperationException("No current element is available.");
                    }
                    return current;
                }
            }

            public async Task<bool> MoveNext(CancellationToken token)
            {
                if (await reader.MoveNext(token))
                {
                    var current = reader.Current;
                    if (onMessage != null)
                    {
                        var mappedValue = onMessage(current);
                        if (mappedValue != null)
                        {
                            current = mappedValue;
                        }
                    }
                    this.current = current;
                    return true;
                }
                onStreamEnd?.Invoke();
                return false;
            }
        }

        private class WrappedClientStreamWriter<T> : IClientStreamWriter<T>
        {
            readonly IClientStreamWriter<T> writer;
            readonly Func<T, T> onMessage;
            readonly Action onResponseStreamEnd;
            public WrappedClientStreamWriter(IClientStreamWriter<T> writer, Func<T, T> onMessage, Action onResponseStreamEnd)
            {
                this.writer = writer;
                this.onMessage = onMessage;
                this.onResponseStreamEnd = onResponseStreamEnd;
            }
            public Task CompleteAsync()
            {
                if (onResponseStreamEnd != null)
                {
                    return writer.CompleteAsync().ContinueWith(x => onResponseStreamEnd());
                }
                return writer.CompleteAsync();
            }
            public Task WriteAsync(T message)
            {
                if (onMessage != null)
                {
                    message = onMessage(message);
                }
                return writer.WriteAsync(message);
            }
            public WriteOptions WriteOptions
            {
                get
                {
                    return writer.WriteOptions;
                }
                set
                {
                    writer.WriteOptions = value;
                }
            }
        }

        private class WrappedAsyncStreamWriter<T> : IServerStreamWriter<T>
        {
            readonly IAsyncStreamWriter<T> writer;
            readonly Func<T, T> onMessage;
            public WrappedAsyncStreamWriter(IAsyncStreamWriter<T> writer, Func<T, T> onMessage)
            {
                this.writer = writer;
                this.onMessage = onMessage;
            }
            public Task WriteAsync(T message)
            {
                if (onMessage != null)
                {
                    message = onMessage(message);
                }
                return writer.WriteAsync(message);
            }
            public WriteOptions WriteOptions
            {
                get
                {
                    return writer.WriteOptions;
                }
                set
                {
                    writer.WriteOptions = value;
                }
            }
        }
    }
}
