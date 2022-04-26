#region Copyright notice and license

// Copyright 2015 gRPC authors.
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
