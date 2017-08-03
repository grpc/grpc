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
using System.Collections.ObjectModel;
using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// Mapping of method names to server call handlers.
    /// Normally, the <c>ServerServiceDefinition</c> objects will be created by the <c>BindService</c> factory method 
    /// that is part of the autogenerated code for a protocol buffers service definition.
    /// </summary>
    public class ServerServiceDefinition
    {
        readonly ReadOnlyDictionary<string, IServerCallHandler> callHandlers;

        private ServerServiceDefinition(Dictionary<string, IServerCallHandler> callHandlers)
        {
            this.callHandlers = new ReadOnlyDictionary<string, IServerCallHandler>(callHandlers);
        }

        internal IDictionary<string, IServerCallHandler> CallHandlers
        {
            get
            {
                return this.callHandlers;
            }
        }

        /// <summary>
        /// Creates a new builder object for <c>ServerServiceDefinition</c>.
        /// </summary>
        /// <returns>The builder object.</returns>
        public static Builder CreateBuilder()
        {
            return new Builder();
        }

        /// <summary>
        /// Builder class for <see cref="ServerServiceDefinition"/>.
        /// </summary>
        public class Builder
        {
            readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();

            /// <summary>
            /// Creates a new instance of builder.
            /// </summary>
            public Builder()
            {
            }

            /// <summary>
            /// Adds a definitions for a single request - single response method.
            /// </summary>
            /// <typeparam name="TRequest">The request message class.</typeparam>
            /// <typeparam name="TResponse">The response message class.</typeparam>
            /// <param name="method">The method.</param>
            /// <param name="handler">The method handler.</param>
            /// <returns>This builder instance.</returns>
            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                UnaryServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.FullName, ServerCalls.UnaryCall(method, handler));
                return this;
            }

            /// <summary>
            /// Adds a definitions for a client streaming method.
            /// </summary>
            /// <typeparam name="TRequest">The request message class.</typeparam>
            /// <typeparam name="TResponse">The response message class.</typeparam>
            /// <param name="method">The method.</param>
            /// <param name="handler">The method handler.</param>
            /// <returns>This builder instance.</returns>
            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                ClientStreamingServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.FullName, ServerCalls.ClientStreamingCall(method, handler));
                return this;
            }

            /// <summary>
            /// Adds a definitions for a server streaming method.
            /// </summary>
            /// <typeparam name="TRequest">The request message class.</typeparam>
            /// <typeparam name="TResponse">The response message class.</typeparam>
            /// <param name="method">The method.</param>
            /// <param name="handler">The method handler.</param>
            /// <returns>This builder instance.</returns>
            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                ServerStreamingServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.FullName, ServerCalls.ServerStreamingCall(method, handler));
                return this;
            }

            /// <summary>
            /// Adds a definitions for a bidirectional streaming method.
            /// </summary>
            /// <typeparam name="TRequest">The request message class.</typeparam>
            /// <typeparam name="TResponse">The response message class.</typeparam>
            /// <param name="method">The method.</param>
            /// <param name="handler">The method handler.</param>
            /// <returns>This builder instance.</returns>
            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                DuplexStreamingServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.FullName, ServerCalls.DuplexStreamingCall(method, handler));
                return this;
            }

            /// <summary>
            /// Creates an immutable <c>ServerServiceDefinition</c> from this builder.
            /// </summary>
            /// <returns>The <c>ServerServiceDefinition</c> object.</returns>
            public ServerServiceDefinition Build()
            {
                return new ServerServiceDefinition(callHandlers);
            }
        }
    }
}
