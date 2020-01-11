#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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
    /// Base class for gRPC channel. Channels are an abstraction of long-lived connections to remote servers.
    /// </summary>
    public abstract class ChannelBase
    {
        private readonly string target;

        /// <summary>
        /// Initializes a new instance of <see cref="ChannelBase"/> class that connects to a specific host.
        /// </summary>
        /// <param name="target">Target of the channel.</param>
        protected ChannelBase(string target)
        {
            this.target = GrpcPreconditions.CheckNotNull(target, nameof(target));
        }

        /// <summary>The original target used to create the channel.</summary>
        public string Target
        {
            get { return this.target; }
        }

        /// <summary>
        /// Create a new <see cref="CallInvoker"/> for the channel.
        /// </summary>
        /// <returns>A new <see cref="CallInvoker"/>.</returns>
        public abstract CallInvoker CreateCallInvoker();
    }
}
