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
    /// Connectivity state of a channel.
    /// Based on grpc_connectivity_state from grpc/grpc.h
    /// </summary>
    public enum ChannelState
    {
        /// <summary>
        /// Channel is idle
        /// </summary>
        Idle,

        /// <summary>
        /// Channel is connecting
        /// </summary>
        Connecting,

        /// <summary>
        /// Channel is ready for work
        /// </summary>
        Ready,

        /// <summary>
        /// Channel has seen a failure but expects to recover
        /// </summary>
        TransientFailure,

        /// <summary>
        /// Channel has seen a failure that it cannot recover from
        /// </summary>
        Shutdown
    }
}
