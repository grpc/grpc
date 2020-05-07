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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Status + metadata + error received on client side when call finishes.
    /// (when receive_status_on_client operation finishes).
    /// </summary>
    internal readonly struct ClientSideStatus
    {
        public ClientSideStatus(Status status, Metadata trailers, string error)
        {
            Status = status;
            Trailers = trailers;
            Error = error;
        }

        public Status Status { get; }

        public Metadata Trailers { get; }

        public string Error { get; }
    }
}
