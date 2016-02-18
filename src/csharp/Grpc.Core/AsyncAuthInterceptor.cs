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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Asynchronous authentication interceptor for <see cref="CallCredentials"/>.
    /// </summary>
    /// <param name="context">The interceptor context.</param>
    /// <param name="metadata">Metadata to populate with entries that will be added to outgoing call's headers.</param>
    /// <returns></returns>
    public delegate Task AsyncAuthInterceptor(AuthInterceptorContext context, Metadata metadata);

    /// <summary>
    /// Context for an RPC being intercepted by <see cref="AsyncAuthInterceptor"/>.
    /// </summary>
    public class AuthInterceptorContext
    {
        readonly string serviceUrl;
        readonly string methodName;

        /// <summary>
        /// Initializes a new instance of <c>AuthInterceptorContext</c>.
        /// </summary>
        public AuthInterceptorContext(string serviceUrl, string methodName)
        {
            this.serviceUrl = GrpcPreconditions.CheckNotNull(serviceUrl);
            this.methodName = GrpcPreconditions.CheckNotNull(methodName);
        }

        /// <summary>
        /// The fully qualified service URL for the RPC being called.
        /// </summary>
        public string ServiceUrl
        {
            get { return serviceUrl; }
        }

        /// <summary>
        /// The method name of the RPC being called.
        /// </summary>
        public string MethodName
        {
            get { return methodName; }
        }
    }
}
