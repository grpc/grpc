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
    /// Carries along the context associated with intercepted invocations on the client side.
    /// This is an EXPERIMENTAL API.
    /// </summary>
    public struct ClientInterceptorContext<TRequest, TResponse>
        where TRequest : class
        where TResponse : class
    {
        /// <summary>
        /// Creates a new instance of <see cref="Grpc.Core.Interceptors.ClientInterceptorContext{TRequest, TResponse}" />
        /// with the specified method, host, and call options.
        /// </summary>
        /// <param name="method">A <see cref="Grpc.Core.Method{TRequest, TResponse}"/> object representing the method to be invoked.</param>
        /// <param name="host">The host to dispatch the current call to.</param>
        /// <param name="options">A <see cref="Grpc.Core.CallOptions"/> instance containing the call options of the current call.</param>
        public ClientInterceptorContext(Method<TRequest, TResponse> method, string host, CallOptions options)
        {
            Method = method;
            Host = host;
            Options = options;
        }

        /// <summary>
        /// Gets the <see cref="Grpc.Core.Method{TRequest, TResponse}"/> instance
        /// representing the method to be invoked.
        /// </summary>
        public Method<TRequest, TResponse> Method { get; }

        /// <summary>
        /// Gets the host that the currect invocation will be dispatched to.
        /// </summary>
        public string Host { get; }

        /// <summary>
        /// Gets the <see cref="Grpc.Core.CallOptions"/> structure representing the
        /// call options associated with the current invocation.
        /// </summary>
        public CallOptions Options { get; }
    }
}
