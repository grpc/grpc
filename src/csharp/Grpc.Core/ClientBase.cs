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
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Grpc.Core
{
    /// <summary>
    /// Interceptor for call headers.
    /// </summary>
    /// <remarks>Header interceptor is no longer to recommented way to perform authentication.
    /// For header (initial metadata) based auth such as OAuth2 or JWT access token, use <see cref="MetadataCredentials"/>.
    /// </remarks>
    public delegate void HeaderInterceptor(IMethod method, Metadata metadata);

    /// <summary>
    /// Base class for client-side stubs.
    /// </summary>
    public abstract class ClientBase
    {
        readonly Channel channel;

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="channel">The channel to use for remote call invocation.</param>
        public ClientBase(Channel channel)
        {
            this.channel = channel;
        }

        /// <summary>
        /// Can be used to register a custom header interceptor.
        /// The interceptor is invoked each time a new call on this client is started.
        /// It is not recommented to use header interceptor to add auth headers to RPC calls.
        /// </summary>
        /// <seealso cref="HeaderInterceptor"/>
        public HeaderInterceptor HeaderInterceptor
        {
            get;
            set;
        }

        /// <summary>
        /// gRPC supports multiple "hosts" being served by a single server. 
        /// This property can be used to set the target host explicitly.
        /// By default, this will be set to <c>null</c> with the meaning
        /// "use default host".
        /// </summary>
        public string Host
        {
            get;
            set;
        }

        /// <summary>
        /// Channel associated with this client.
        /// </summary>
        public Channel Channel
        {
            get
            {
                return this.channel;
            }
        }

        /// <summary>
        /// Creates a new call to given method.
        /// </summary>
        /// <param name="method">The method to invoke.</param>
        /// <param name="options">The call options.</param>
        /// <typeparam name="TRequest">Request message type.</typeparam>
        /// <typeparam name="TResponse">Response message type.</typeparam>
        /// <returns>The call invocation details.</returns>
        protected CallInvocationDetails<TRequest, TResponse> CreateCall<TRequest, TResponse>(Method<TRequest, TResponse> method, CallOptions options)
            where TRequest : class
            where TResponse : class
        {
            var interceptor = HeaderInterceptor;
            if (interceptor != null)
            {
                if (options.Headers == null)
                {
                    options = options.WithHeaders(new Metadata());
                }
                interceptor(method, options.Headers);
            }
            return new CallInvocationDetails<TRequest, TResponse>(channel, method, Host, options);
        }
    }
}
