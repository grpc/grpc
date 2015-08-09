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
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Method types supported by gRPC.
    /// </summary>
    public enum MethodType
    {
        Unary,  // Unary request, unary response.
        ClientStreaming,  // Streaming request, unary response.
        ServerStreaming,  // Unary request, streaming response.
        DuplexStreaming  // Streaming request, streaming response.
    }

    /// <summary>
    /// A description of a service method.
    /// </summary>
    public class Method<TRequest, TResponse>
    {
        readonly MethodType type;
        readonly string serviceName;
        readonly string name;
        readonly Marshaller<TRequest> requestMarshaller;
        readonly Marshaller<TResponse> responseMarshaller;
        readonly string fullName;

        public Method(MethodType type, string serviceName, string name, Marshaller<TRequest> requestMarshaller, Marshaller<TResponse> responseMarshaller)
        {
            this.type = type;
            this.serviceName = Preconditions.CheckNotNull(serviceName);
            this.name = Preconditions.CheckNotNull(name);
            this.requestMarshaller = Preconditions.CheckNotNull(requestMarshaller);
            this.responseMarshaller = Preconditions.CheckNotNull(responseMarshaller);
            this.fullName = GetFullName(serviceName);
        }

        public MethodType Type
        {
            get
            {
                return this.type;
            }
        }
            
        public string ServiceName
        {
            get
            {
                return this.serviceName;
            }
        }

        public string Name
        {
            get
            {
                return this.name;
            }
        }

        public Marshaller<TRequest> RequestMarshaller
        {
            get
            {
                return this.requestMarshaller;
            }
        }

        public Marshaller<TResponse> ResponseMarshaller
        {
            get
            {
                return this.responseMarshaller;
            }
        }

        public string FullName
        {
            get
            {
                return this.fullName;
            }
        }

        /// <summary>
        /// Gets full name of the method including the service name.
        /// </summary>
        internal string GetFullName(string serviceName)
        {
            return "/" + Preconditions.CheckNotNull(serviceName) + "/" + this.Name;
        }
    }
}
