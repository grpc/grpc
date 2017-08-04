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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Flags to enable special call behaviors (client-side only).
    /// </summary>
    [Flags]
    internal enum CallFlags
    {
        /// <summary>
        /// The call is idempotent (retrying the call doesn't change the outcome of the operation).
        /// </summary>
        IdempotentRequest = 0x10,

        /// <summary>
        /// If channel is in <c>ChannelState.TransientFailure</c>, attempt waiting for the channel to recover
        /// instead of failing the call immediately.
        /// </summary>
        WaitForReady = 0x20,

        /// <summary>
        /// The call is cacheable. gRPC is free to use GET verb */
        /// </summary>
        CacheableRequest = 0x40
    }
}
