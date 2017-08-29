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
        public RpcException(Status status) : base(status.ToString())
        {
            this.status = status;
            this.trailers = Metadata.Empty;
        }

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status and message.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        /// <param name="message">The exception message.</param> 
        public RpcException(Status status, string message) : base(message)
        {
            this.status = status;
            this.trailers = Metadata.Empty;
        }

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status and trailing response metadata.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        /// <param name="trailers">Response trailing metadata.</param> 
        public RpcException(Status status, Metadata trailers) : base(status.ToString())
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
