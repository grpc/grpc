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
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Abstraction of a call to be invoked on a client.
    /// </summary>
    public class Call<TRequest, TResponse>
    {
        readonly string name;
        readonly Marshaller<TRequest> requestMarshaller;
        readonly Marshaller<TResponse> responseMarshaller;
        readonly Channel channel;
        readonly CallContext context;

        public Call(string serviceName, Method<TRequest, TResponse> method, Channel channel, CallContext context)
        {
            this.name = method.GetFullName(serviceName);
            this.requestMarshaller = method.RequestMarshaller;
            this.responseMarshaller = method.ResponseMarshaller;
            this.channel = channel;
            this.context = context;
        }

        public Channel Channel
        {
            get
            {
                return this.channel;
            }
        }

        /// <summary>
        /// Full methods name including the service name.
        /// </summary>
        public string Name
        {
            get
            {
                return name;
            }
        }

        /// <summary>
        /// Call context.
        /// </summary>
        public CallContext Context
        {
            get
            {
                return context;
            }
        }

        public Marshaller<TRequest> RequestMarshaller
        {
            get
            {
                return requestMarshaller;
            }
        }

        public Marshaller<TResponse> ResponseMarshaller
        {
            get
            {
                return responseMarshaller;
            }
        }
    }
}
