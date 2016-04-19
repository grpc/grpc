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
        /// <summary>Single request sent from client, single response received from server.</summary>
        Unary,

        /// <summary>Stream of request sent from client, single response received from server.</summary>
        ClientStreaming,

        /// <summary>Single request sent from client, stream of responses received from server.</summary>
        ServerStreaming,

        /// <summary>Both server and client can stream arbitrary number of requests and responses simultaneously.</summary>
        DuplexStreaming
    }

    /// <summary>
    /// A non-generic representation of a remote method.
    /// </summary>
    public interface IMethod
    {
        /// <summary>
        /// Gets the type of the method.
        /// </summary>
        MethodType Type { get; }

        /// <summary>
        /// Gets the name of the service to which this method belongs.
        /// </summary>
        string ServiceName { get; }

        /// <summary>
        /// Gets the unqualified name of the method.
        /// </summary>
        string Name { get; }

        /// <summary>
        /// Gets the fully qualified name of the method. On the server side, methods are dispatched
        /// based on this name.
        /// </summary>
        string FullName { get; }
    }

    /// <summary>
    /// A description of a remote method.
    /// </summary>
    /// <typeparam name="TRequest">Request message type for this method.</typeparam>
    /// <typeparam name="TResponse">Response message type for this method.</typeparam>
    public class Method<TRequest, TResponse> : IMethod
    {
        readonly MethodType type;
        readonly string serviceName;
        readonly string name;
        readonly Marshaller<TRequest> requestMarshaller;
        readonly Marshaller<TResponse> responseMarshaller;
        readonly string fullName;

        /// <summary>
        /// Initializes a new instance of the <c>Method</c> class.
        /// </summary>
        /// <param name="type">Type of method.</param>
        /// <param name="serviceName">Name of service this method belongs to.</param>
        /// <param name="name">Unqualified name of the method.</param>
        /// <param name="requestMarshaller">Marshaller used for request messages.</param>
        /// <param name="responseMarshaller">Marshaller used for response messages.</param>
        public Method(MethodType type, string serviceName, string name, Marshaller<TRequest> requestMarshaller, Marshaller<TResponse> responseMarshaller)
        {
            this.type = type;
            this.serviceName = GrpcPreconditions.CheckNotNull(serviceName, "serviceName");
            this.name = GrpcPreconditions.CheckNotNull(name, "name");
            this.requestMarshaller = GrpcPreconditions.CheckNotNull(requestMarshaller, "requestMarshaller");
            this.responseMarshaller = GrpcPreconditions.CheckNotNull(responseMarshaller, "responseMarshaller");
            this.fullName = GetFullName(serviceName, name);
        }

        /// <summary>
        /// Gets the type of the method.
        /// </summary>
        public MethodType Type
        {
            get
            {
                return this.type;
            }
        }
            
        /// <summary>
        /// Gets the name of the service to which this method belongs.
        /// </summary>
        public string ServiceName
        {
            get
            {
                return this.serviceName;
            }
        }

        /// <summary>
        /// Gets the unqualified name of the method.
        /// </summary>
        public string Name
        {
            get
            {
                return this.name;
            }
        }

        /// <summary>
        /// Gets the marshaller used for request messages.
        /// </summary>
        public Marshaller<TRequest> RequestMarshaller
        {
            get
            {
                return this.requestMarshaller;
            }
        }

        /// <summary>
        /// Gets the marshaller used for response messages.
        /// </summary>
        public Marshaller<TResponse> ResponseMarshaller
        {
            get
            {
                return this.responseMarshaller;
            }
        }
            
        /// <summary>
        /// Gets the fully qualified name of the method. On the server side, methods are dispatched
        /// based on this name.
        /// </summary>
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
        internal static string GetFullName(string serviceName, string methodName)
        {
            return "/" + serviceName + "/" + methodName;
        }
    }
}
