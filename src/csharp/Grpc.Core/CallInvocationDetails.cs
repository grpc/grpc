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
    /// Details about a client-side call to be invoked.
    /// </summary>
    /// <typeparam name="TRequest">Request message type for the call.</typeparam>
    /// <typeparam name="TResponse">Response message type for the call.</typeparam>
    public struct CallInvocationDetails<TRequest, TResponse>
    {
        readonly Channel channel;
        readonly string method;
        readonly string host;
        readonly Marshaller<TRequest> requestMarshaller;
        readonly Marshaller<TResponse> responseMarshaller;
        CallOptions options;

        /// <summary>
        /// Initializes a new instance of the <see cref="Grpc.Core.CallInvocationDetails{TRequest,TResponse}"/> struct.
        /// </summary>
        /// <param name="channel">Channel to use for this call.</param>
        /// <param name="method">Method to call.</param>
        /// <param name="options">Call options.</param>
        public CallInvocationDetails(Channel channel, Method<TRequest, TResponse> method, CallOptions options) :
            this(channel, method, null, options)
        {
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="Grpc.Core.CallInvocationDetails{TRequest,TResponse}"/> struct.
        /// </summary>
        /// <param name="channel">Channel to use for this call.</param>
        /// <param name="method">Method to call.</param>
        /// <param name="host">Host that contains the method. if <c>null</c>, default host will be used.</param>
        /// <param name="options">Call options.</param>
        public CallInvocationDetails(Channel channel, Method<TRequest, TResponse> method, string host, CallOptions options) :
            this(channel, method.FullName, host, method.RequestMarshaller, method.ResponseMarshaller, options)
        {
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="Grpc.Core.CallInvocationDetails{TRequest,TResponse}"/> struct.
        /// </summary>
        /// <param name="channel">Channel to use for this call.</param>
        /// <param name="method">Qualified method name.</param>
        /// <param name="host">Host that contains the method.</param>
        /// <param name="requestMarshaller">Request marshaller.</param>
        /// <param name="responseMarshaller">Response marshaller.</param>
        /// <param name="options">Call options.</param>
        public CallInvocationDetails(Channel channel, string method, string host, Marshaller<TRequest> requestMarshaller, Marshaller<TResponse> responseMarshaller, CallOptions options)
        {
            this.channel = GrpcPreconditions.CheckNotNull(channel, "channel");
            this.method = GrpcPreconditions.CheckNotNull(method, "method");
            this.host = host;
            this.requestMarshaller = GrpcPreconditions.CheckNotNull(requestMarshaller, "requestMarshaller");
            this.responseMarshaller = GrpcPreconditions.CheckNotNull(responseMarshaller, "responseMarshaller");
            this.options = options;
        }

        /// <summary>
        /// Get channel associated with this call.
        /// </summary>
        public Channel Channel
        {
            get
            {
                return this.channel;
            }
        }

        /// <summary>
        /// Gets name of method to be called.
        /// </summary>
        public string Method
        {
            get
            {
                return this.method;
            }
        }

        /// <summary>
        /// Get name of host.
        /// </summary>
        public string Host
        {
            get
            {
                return this.host;
            }
        }

        /// <summary>
        /// Gets marshaller used to serialize requests.
        /// </summary>
        public Marshaller<TRequest> RequestMarshaller
        {
            get
            {
                return this.requestMarshaller;
            }
        }

        /// <summary>
        /// Gets marshaller used to deserialized responses.
        /// </summary>
        public Marshaller<TResponse> ResponseMarshaller
        {
            get
            {
                return this.responseMarshaller;
            }
        }
            
        /// <summary>
        /// Gets the call options.
        /// </summary>
        public CallOptions Options
        {
            get
            {
                return options;
            }
        }

        /// <summary>
        /// Returns new instance of <see cref="CallInvocationDetails{TRequest, TResponse}"/> with
        /// <c>Options</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        public CallInvocationDetails<TRequest, TResponse> WithOptions(CallOptions options)
        {
            var newDetails = this;
            newDetails.options = options;
            return newDetails;
        }
    }
}
