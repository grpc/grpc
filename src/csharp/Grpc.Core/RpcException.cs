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

namespace Grpc.Core
{
    /// <summary>
    /// Thrown when remote procedure call fails. Every <c>RpcException</c> is associated with a resulting <see cref="Status"/> of the call.
    /// </summary>
    public class RpcException : Exception
    {
        private readonly Status status;

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        public RpcException(Status status) : base(status.ToString())
        {
            this.status = status;
        }

        /// <summary>
        /// Creates a new <c>RpcException</c> associated with given status and message.
        /// </summary>
        /// <param name="status">Resulting status of a call.</param>
        /// <param name="message">The exception message.</param> 
        public RpcException(Status status, string message) : base(message)
        {
            this.status = status;
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
    }
}
