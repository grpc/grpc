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
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal interface IServerCallHandler
    {
        Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq);
    }

    internal class UnaryServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<UnaryServerCallHandler<TRequest, TResponse>>();

        readonly Method<TRequest, TResponse> method;
        readonly UnaryServerMethod<TRequest, TResponse> handler;

        public UnaryServerCallHandler(Method<TRequest, TResponse> method, UnaryServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer,
                newRpc.Server);

            asyncCall.Initialize(newRpc.Call, cq);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);

            Status status;
            Tuple<TResponse,WriteFlags> responseTuple = null;
            var context = HandlerUtils.NewContext(newRpc, responseStream, asyncCall.CancellationToken);
            try
            {
                GrpcPreconditions.CheckArgument(await requestStream.MoveNext().ConfigureAwait(false));
                var request = requestStream.Current;
                var response = await handler(request, context).ConfigureAwait(false);
                status = context.Status;
                responseTuple = Tuple.Create(response, HandlerUtils.GetWriteFlags(context.WriteOptions));
            } 
            catch (Exception e)
            {
                if (!(e is RpcException))
                {
                    Logger.Warning(e, "Exception occured in handler.");
                }
                status = HandlerUtils.StatusFromException(e);
            }
            try
            {
                await asyncCall.SendStatusFromServerAsync(status, context.ResponseTrailers, responseTuple).ConfigureAwait(false);
            }
            catch (Exception)
            {
                asyncCall.Cancel();
                throw;
            }
            await finishedTask.ConfigureAwait(false);
        }
    }

    internal class ServerStreamingServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<ServerStreamingServerCallHandler<TRequest, TResponse>>();

        readonly Method<TRequest, TResponse> method;
        readonly ServerStreamingServerMethod<TRequest, TResponse> handler;

        public ServerStreamingServerCallHandler(Method<TRequest, TResponse> method, ServerStreamingServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer,
                newRpc.Server);

            asyncCall.Initialize(newRpc.Call, cq);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);

            Status status;
            var context = HandlerUtils.NewContext(newRpc, responseStream, asyncCall.CancellationToken);
            try
            {
                GrpcPreconditions.CheckArgument(await requestStream.MoveNext().ConfigureAwait(false));
                var request = requestStream.Current;
                await handler(request, responseStream, context).ConfigureAwait(false);
                status = context.Status;
            }
            catch (Exception e)
            {
                if (!(e is RpcException))
                {
                    Logger.Warning(e, "Exception occured in handler.");
                }
                status = HandlerUtils.StatusFromException(e);
            }

            try
            {
                await asyncCall.SendStatusFromServerAsync(status, context.ResponseTrailers, null).ConfigureAwait(false);
            }
            catch (Exception)
            {
                asyncCall.Cancel();
                throw;
            }
            await finishedTask.ConfigureAwait(false);
        }
    }

    internal class ClientStreamingServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<ClientStreamingServerCallHandler<TRequest, TResponse>>();

        readonly Method<TRequest, TResponse> method;
        readonly ClientStreamingServerMethod<TRequest, TResponse> handler;

        public ClientStreamingServerCallHandler(Method<TRequest, TResponse> method, ClientStreamingServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer,
                newRpc.Server);

            asyncCall.Initialize(newRpc.Call, cq);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);

            Status status;
            Tuple<TResponse,WriteFlags> responseTuple = null;
            var context = HandlerUtils.NewContext(newRpc, responseStream, asyncCall.CancellationToken);
            try
            {
                var response = await handler(requestStream, context).ConfigureAwait(false);
                status = context.Status;
                responseTuple = Tuple.Create(response, HandlerUtils.GetWriteFlags(context.WriteOptions));
            }
            catch (Exception e)
            {
                if (!(e is RpcException))
                {
                    Logger.Warning(e, "Exception occured in handler.");
                }
                status = HandlerUtils.StatusFromException(e);
            }

            try
            {
                await asyncCall.SendStatusFromServerAsync(status, context.ResponseTrailers, responseTuple).ConfigureAwait(false);
            }
            catch (Exception)
            {
                asyncCall.Cancel();
                throw;
            }
            await finishedTask.ConfigureAwait(false);
        }
    }

    internal class DuplexStreamingServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<DuplexStreamingServerCallHandler<TRequest, TResponse>>();

        readonly Method<TRequest, TResponse> method;
        readonly DuplexStreamingServerMethod<TRequest, TResponse> handler;

        public DuplexStreamingServerCallHandler(Method<TRequest, TResponse> method, DuplexStreamingServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer,
                newRpc.Server);

            asyncCall.Initialize(newRpc.Call, cq);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);

            Status status;
            var context = HandlerUtils.NewContext(newRpc, responseStream, asyncCall.CancellationToken);
            try
            {
                await handler(requestStream, responseStream, context).ConfigureAwait(false);
                status = context.Status;
            }
            catch (Exception e)
            {
                if (!(e is RpcException))
                {
                    Logger.Warning(e, "Exception occured in handler.");
                }
                status = HandlerUtils.StatusFromException(e);
            }
            try
            {
                await asyncCall.SendStatusFromServerAsync(status, context.ResponseTrailers, null).ConfigureAwait(false);
            }
            catch (Exception)
            {
                asyncCall.Cancel();
                throw;
            }
            await finishedTask.ConfigureAwait(false);
        }
    }

    internal class UnimplementedMethodCallHandler : IServerCallHandler
    {
        public static readonly UnimplementedMethodCallHandler Instance = new UnimplementedMethodCallHandler();

        DuplexStreamingServerCallHandler<byte[], byte[]> callHandlerImpl;

        public UnimplementedMethodCallHandler()
        {
            var marshaller = new Marshaller<byte[]>((payload) => payload, (payload) => payload);
            var method = new Method<byte[], byte[]>(MethodType.DuplexStreaming, "", "", marshaller, marshaller);
            this.callHandlerImpl = new DuplexStreamingServerCallHandler<byte[], byte[]>(method, new DuplexStreamingServerMethod<byte[], byte[]>(UnimplementedMethod));
        }

        /// <summary>
        /// Handler used for unimplemented method.
        /// </summary>
        private Task UnimplementedMethod(IAsyncStreamReader<byte[]> requestStream, IServerStreamWriter<byte[]> responseStream, ServerCallContext ctx)
        {
            ctx.Status = new Status(StatusCode.Unimplemented, "");
            return TaskUtils.CompletedTask;
        }

        public Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
        {
            return callHandlerImpl.HandleCall(newRpc, cq);
        }
    }

    internal static class HandlerUtils
    {
        public static Status StatusFromException(Exception e)
        {
            var rpcException = e as RpcException;
            if (rpcException != null)
            {
                // use the status thrown by handler.
                return rpcException.Status;
            }

            return new Status(StatusCode.Unknown, "Exception was thrown by handler.");
        }

        public static WriteFlags GetWriteFlags(WriteOptions writeOptions)
        {
            return writeOptions != null ? writeOptions.Flags : default(WriteFlags);
        }

        public static ServerCallContext NewContext<TRequest, TResponse>(ServerRpcNew newRpc, ServerResponseStream<TRequest, TResponse> serverResponseStream, CancellationToken cancellationToken)
            where TRequest : class
            where TResponse : class
        {
            DateTime realtimeDeadline = newRpc.Deadline.ToClockType(ClockType.Realtime).ToDateTime();

            return new ServerCallContext(newRpc.Call, newRpc.Method, newRpc.Host, realtimeDeadline,
                newRpc.RequestMetadata, cancellationToken, serverResponseStream.WriteResponseHeadersAsync, serverResponseStream);
        }
    }
}
