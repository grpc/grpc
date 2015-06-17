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
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal interface IServerCallHandler
    {
        Task HandleCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq);
    }

    internal class UnaryServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        readonly Method<TRequest, TResponse> method;
        readonly UnaryServerMethod<TRequest, TResponse> handler;

        public UnaryServerCallHandler(Method<TRequest, TResponse> method, UnaryServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer);

            asyncCall.Initialize(call);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);

            Status status = Status.DefaultSuccess;
            try
            {
                Preconditions.CheckArgument(await requestStream.MoveNext());
                var request = requestStream.Current;
                // TODO(jtattermusch): we need to read the full stream so that native callhandle gets deallocated.
                Preconditions.CheckArgument(!await requestStream.MoveNext());
                var context = new ServerCallContext();  // TODO(jtattermusch): initialize the context
                var result = await handler(context, request);
                await responseStream.WriteAsync(result);
            } 
            catch (Exception e)
            {
                Console.WriteLine("Exception occured in handler: " + e);
                status = HandlerUtils.StatusFromException(e);
            }
            try
            {
                await responseStream.WriteStatusAsync(status);
            }
            catch (OperationCanceledException)
            {
                // Call has been already cancelled.
            }
            await finishedTask;
        }
    }

    internal class ServerStreamingServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        readonly Method<TRequest, TResponse> method;
        readonly ServerStreamingServerMethod<TRequest, TResponse> handler;

        public ServerStreamingServerCallHandler(Method<TRequest, TResponse> method, ServerStreamingServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer);

            asyncCall.Initialize(call);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);

            Status status = Status.DefaultSuccess;
            try
            {
                Preconditions.CheckArgument(await requestStream.MoveNext());
                var request = requestStream.Current;
                // TODO(jtattermusch): we need to read the full stream so that native callhandle gets deallocated.
                Preconditions.CheckArgument(!await requestStream.MoveNext());

                var context = new ServerCallContext();  // TODO(jtattermusch): initialize the context
                await handler(context, request, responseStream);
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception occured in handler: " + e);
                status = HandlerUtils.StatusFromException(e);
            }

            try
            {
                await responseStream.WriteStatusAsync(status);
            }
            catch (OperationCanceledException)
            {
                // Call has been already cancelled.
            }
            await finishedTask;
        }
    }

    internal class ClientStreamingServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        readonly Method<TRequest, TResponse> method;
        readonly ClientStreamingServerMethod<TRequest, TResponse> handler;

        public ClientStreamingServerCallHandler(Method<TRequest, TResponse> method, ClientStreamingServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer);

            asyncCall.Initialize(call);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);
            var context = new ServerCallContext();  // TODO(jtattermusch): initialize the context

            Status status = Status.DefaultSuccess;
            try
            {
                var result = await handler(context, requestStream);
                try
                {
                    await responseStream.WriteAsync(result);
                }
                catch (OperationCanceledException)
                {
                    status = Status.DefaultCancelled;
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception occured in handler: " + e);
                status = HandlerUtils.StatusFromException(e);
            }

            try
            {
                await responseStream.WriteStatusAsync(status);
            }
            catch (OperationCanceledException)
            {
                // Call has been already cancelled.
            }
            await finishedTask;
        }
    }

    internal class DuplexStreamingServerCallHandler<TRequest, TResponse> : IServerCallHandler
        where TRequest : class
        where TResponse : class
    {
        readonly Method<TRequest, TResponse> method;
        readonly DuplexStreamingServerMethod<TRequest, TResponse> handler;

        public DuplexStreamingServerCallHandler(Method<TRequest, TResponse> method, DuplexStreamingServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public async Task HandleCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCallServer<TRequest, TResponse>(
                method.ResponseMarshaller.Serializer,
                method.RequestMarshaller.Deserializer);

            asyncCall.Initialize(call);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<TRequest, TResponse>(asyncCall);
            var responseStream = new ServerResponseStream<TRequest, TResponse>(asyncCall);
            var context = new ServerCallContext();  // TODO(jtattermusch): initialize the context

            Status status = Status.DefaultSuccess;
            try
            {
                await handler(context, requestStream, responseStream);
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception occured in handler: " + e);
                status = HandlerUtils.StatusFromException(e);
            }
            try
            {
                await responseStream.WriteStatusAsync(status);
            }
            catch (OperationCanceledException)
            {
                // Call has been already cancelled.
            }
            await finishedTask;
        }
    }

    internal class NoSuchMethodCallHandler : IServerCallHandler
    {
        public async Task HandleCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            // We don't care about the payload type here.
            var asyncCall = new AsyncCallServer<byte[], byte[]>(
                (payload) => payload, (payload) => payload);
            
            asyncCall.Initialize(call);
            var finishedTask = asyncCall.ServerSideCallAsync();
            var requestStream = new ServerRequestStream<byte[], byte[]>(asyncCall);
            var responseStream = new ServerResponseStream<byte[], byte[]>(asyncCall);

            await responseStream.WriteStatusAsync(new Status(StatusCode.Unimplemented, "No such method."));
            await finishedTask;
        }
    }

    internal static class HandlerUtils
    {
        public static Status StatusFromException(Exception e)
        {
            // TODO(jtattermusch): what is the right status code here?
            return new Status(StatusCode.Unknown, "Exception was thrown by handler.");
        }
    }
}
