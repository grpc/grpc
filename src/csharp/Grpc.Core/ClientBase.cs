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

using Grpc.Core.Internal;
using System.Text.RegularExpressions;

namespace Grpc.Core
{
    public delegate void MetadataInterceptorDelegate(string authUri, Metadata metadata);

    /// <summary>
    /// Base class for client-side stubs.
    /// </summary>
    public abstract class ClientBase
    {
        static readonly Regex TrailingPortPattern = new Regex(":[0-9]+/?$");
        readonly Channel channel;
        readonly string authUriBase;

        public ClientBase(Channel channel)
        {
            this.channel = channel;
            // TODO(jtattermush): we shouldn't need to hand-curate the channel.Target contents.
            this.authUriBase = "https://" + TrailingPortPattern.Replace(channel.Target, "") + "/";
        }

        /// <summary>
        /// Can be used to register a custom header (initial metadata) interceptor.
        /// The delegate each time before a new call on this client is started.
        /// </summary>
        public MetadataInterceptorDelegate HeaderInterceptor
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
                var authUri = authUriBase + method.ServiceName;
                interceptor(authUri, options.Headers);
            }
            return new CallInvocationDetails<TRequest, TResponse>(channel, method, Host, options);
        }
    }
}
