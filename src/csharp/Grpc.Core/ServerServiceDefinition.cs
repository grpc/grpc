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
        readonly string prefixName;
        readonly ReadOnlyDictionary<string, IServerCallHandler> callHandlers;

        private ServerServiceDefinition(string prefixName, Dictionary<string, IServerCallHandler> callHandlers)
        {
            this.prefixName = prefixName;
            this.callHandlers = new ReadOnlyDictionary<string, IServerCallHandler>(callHandlers);
        }

        /// <summary>
        /// The prefix name to be used before the methods full name.
        /// </summary>
        public string PrefixName
        {
            get { return this.prefixName; }
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
        /// <param name="prefixName">The prefix name.</param>
        public static Builder CreateBuilder(string prefixName = null)
        {
            return new Builder(prefixName);
        }

        /// <summary>
        /// Builder class for <see cref="ServerServiceDefinition"/>.
        /// </summary>
        public class Builder
        {
            private string prefixName;
            private string prefix;

            readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();

            /// <summary>
            /// Creates a new instance of builder.
            /// </summary>
            /// <param name="prefixName">The prefix name.</param>
            public Builder(string prefixName)
            {
                this.prefixName = prefixName;
                this.prefix = prefixName != null ? "/" + prefixName : string.Empty;
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
                callHandlers.Add(prefix + method.FullName, ServerCalls.UnaryCall(method, handler));
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
                callHandlers.Add(prefix + method.FullName, ServerCalls.ClientStreamingCall(method, handler));
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
                callHandlers.Add(prefix + method.FullName, ServerCalls.ServerStreamingCall(method, handler));
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
                callHandlers.Add(prefix + method.FullName, ServerCalls.DuplexStreamingCall(method, handler));
                return this;
            }

            /// <summary>
            /// Creates an immutable <c>ServerServiceDefinition</c> from this builder.
            /// </summary>
            /// <returns>The <c>ServerServiceDefinition</c> object.</returns>
            public ServerServiceDefinition Build()
            {
                return new ServerServiceDefinition(prefixName, callHandlers);
            }
        }
    }
}
