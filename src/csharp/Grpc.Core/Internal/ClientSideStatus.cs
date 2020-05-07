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
using Grpc.Core;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Status + metadata received on client side when call finishes.
    /// (when receive_status_on_client operation finishes).
    /// </summary>
    internal struct ClientSideStatus
    {
        readonly Status status;
        readonly Metadata trailers;

        public ClientSideStatus(Status status, Metadata trailers)
        {
            this.status = status;
            this.trailers = trailers;
        }

        public Status Status
        {
            get
            {
                return this.status;
            }
        }

        public Metadata Trailers
        {
            get
            {
                return this.trailers;
            }
        }
    }
}
