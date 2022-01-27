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
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Thrown when remote procedure call fails. Every <c>RpcException</c> is associated with a resulting <see cref="Status"/> of the call.
    /// </summary>
    public class RpcException : Exception
    {
        private readonly Status status;
        private readonly Metadata trailers;

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        public RpcException(Status status) : this(status, Metadata.Empty, status.ToString())
        {
        }

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status and message.
        /// NOTE: the exception message is not sent to the remote peer. Use <c>status.Details</c> to pass error
        /// details to the peer.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        /// <param name="message">The exception message.</param> 
        public RpcException(Status status, string message) : this(status, Metadata.Empty, message)
        {
        }

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status and trailing response metadata.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        /// <param name="trailers">Response trailing metadata.</param> 
        public RpcException(Status status, Metadata trailers) : this(status, trailers, status.ToString())
        {
        }

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status, message and trailing response metadata.
        /// NOTE: the exception message is not sent to the remote peer. Use <c>status.Details</c> to pass error
        /// details to the peer.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        /// <param name="trailers">Response trailing metadata.</param>
        /// <param name="message">The exception message.</param>
        public RpcException(Status status, Metadata trailers, string message) : base(message)
        {
            this.status = status;
            this.trailers = GrpcPreconditions.CheckNotNull(trailers);
        }

        /// <summary>
        /// Resulting status of the call.
        /// </summary>
        public Status Status
        {
            get
            {
                return status;
            }
        }

        /// <summary>
        /// Returns the status code of the call, as a convenient alternative to <see cref="StatusCode">Status.StatusCode</see>.
        /// </summary>
        public StatusCode StatusCode
        {
            get
            {
                return status.StatusCode;
            }
        }

        /// <summary>
        /// Gets the call trailing metadata.
        /// Trailers only have meaningful content for client-side calls (in which case they represent the trailing metadata sent by the server when closing the call).
        /// Instances of <c>RpcException</c> thrown by the server-side part of the stack will have trailers always set to empty.
        /// </summary>
        public Metadata Trailers
        {
            get
            {
                return trailers;
            }
        }
    }
}
