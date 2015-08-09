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

        public static Builder CreateBuilder(string serviceName)
        {
            return new Builder(serviceName);
        }

        public class Builder
        {
            readonly string serviceName;
            readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();

            public Builder(string serviceName)
            {
                this.serviceName = serviceName;
            }

            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                UnaryServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.GetFullName(serviceName), ServerCalls.UnaryCall(method, handler));
                return this;
            }

            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                ClientStreamingServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.GetFullName(serviceName), ServerCalls.ClientStreamingCall(method, handler));
                return this;
            }

            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                ServerStreamingServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.GetFullName(serviceName), ServerCalls.ServerStreamingCall(method, handler));
                return this;
            }

            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method,
                DuplexStreamingServerMethod<TRequest, TResponse> handler)
                    where TRequest : class
                    where TResponse : class
            {
                callHandlers.Add(method.GetFullName(serviceName), ServerCalls.DuplexStreamingCall(method, handler));
                return this;
            }

            public ServerServiceDefinition Build()
            {
                return new ServerServiceDefinition(callHandlers);
            }
        }
    }
}
