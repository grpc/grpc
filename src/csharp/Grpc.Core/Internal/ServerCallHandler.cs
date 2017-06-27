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
