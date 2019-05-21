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
    /// Extends the ServerServiceDefinition class to add methods used to register interceptors on the server side.
    /// </summary>
    public static class ServerServiceDefinitionExtensions
    {
        /// <summary>
        /// Returns a <see cref="Grpc.Core.ServerServiceDefinition" /> instance that
        /// intercepts incoming calls to the underlying service handler through the given interceptor.
        /// </summary>
        /// <param name="serverServiceDefinition">The <see cref="Grpc.Core.ServerServiceDefinition" /> instance to register interceptors on.</param>
        /// <param name="interceptor">The interceptor to intercept the incoming invocations with.</param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "serverServiceDefinition.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted service definition, effectively
        /// building a chain like "serverServiceDefinition.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static ServerServiceDefinition Intercept(this ServerServiceDefinition serverServiceDefinition, Interceptor interceptor)
        {
            GrpcPreconditions.CheckNotNull(serverServiceDefinition, nameof(serverServiceDefinition));
            GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));

            var binder = new InterceptingServiceBinder(interceptor);
            serverServiceDefinition.BindService(binder);
            return binder.GetInterceptedServerServiceDefinition();
        }

        /// <summary>
        /// Returns a <see cref="Grpc.Core.ServerServiceDefinition" /> instance that
        /// intercepts incoming calls to the underlying service handler through the given interceptors.
        /// </summary>
        /// <param name="serverServiceDefinition">The <see cref="Grpc.Core.ServerServiceDefinition" /> instance to register interceptors on.</param>
        /// <param name="interceptors">
        /// An array of interceptors to intercept the incoming invocations with.
        /// Control is passed to the interceptors in the order specified.
        /// </param>
        /// <remarks>
        /// Multiple interceptors can be added on top of each other by calling
        /// "serverServiceDefinition.Intercept(a, b, c)".  The order of invocation will be "a", "b", and then "c".
        /// Interceptors can be later added to an existing intercepted service definition, effectively
        /// building a chain like "serverServiceDefinition.Intercept(c).Intercept(b).Intercept(a)".  Note that
        /// in this case, the last interceptor added will be the first to take control.
        /// </remarks>
        public static ServerServiceDefinition Intercept(this ServerServiceDefinition serverServiceDefinition, params Interceptor[] interceptors)
        {
            GrpcPreconditions.CheckNotNull(serverServiceDefinition, nameof(serverServiceDefinition));
            GrpcPreconditions.CheckNotNull(interceptors, nameof(interceptors));

            foreach (var interceptor in interceptors.Reverse())
            {
                serverServiceDefinition = Intercept(serverServiceDefinition, interceptor);
            }

            return serverServiceDefinition;
        }

        /// <summary>
        /// Helper for creating <c>ServerServiceDefinition</c> with intercepted handlers.
        /// </summary>
        private class InterceptingServiceBinder : ServiceBinderBase
        {
            readonly ServerServiceDefinition.Builder builder = ServerServiceDefinition.CreateBuilder();
            readonly Interceptor interceptor;

            public InterceptingServiceBinder(Interceptor interceptor)
            {
                this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));
            }

            internal ServerServiceDefinition GetInterceptedServerServiceDefinition()
            {
                return builder.Build();
            }

            public override void AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                UnaryServerMethod<TRequest, TResponse> handler)
            {
                builder.AddMethod(method, (request, context) => interceptor.UnaryServerHandler(request, context, handler));
            }

            public override void AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                ClientStreamingServerMethod<TRequest, TResponse> handler)
            {
                builder.AddMethod(method, (requestStream, context) => interceptor.ClientStreamingServerHandler(requestStream, context, handler));
            }

            public override void AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                ServerStreamingServerMethod<TRequest, TResponse> handler)
            {
                builder.AddMethod(method, (request, responseStream, context) => interceptor.ServerStreamingServerHandler(request, responseStream, context, handler));
            }

            public override void AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                DuplexStreamingServerMethod<TRequest, TResponse> handler)
            {
                builder.AddMethod(method, (requestStream, responseStream, context) => interceptor.DuplexStreamingServerHandler(requestStream, responseStream, context, handler));
            }
        }
    }
}
