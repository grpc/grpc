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
using System.Threading.Tasks;
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

        /// <summary>
        /// Shuts down the channel cleanly. It is strongly recommended to shutdown
        /// the channel once you stopped using it.
        /// </summary>
        /// <remarks>
        /// Guidance for implementors:
        /// This method doesn't wait for all calls on this channel to finish (nor does
        /// it have to explicitly cancel all outstanding calls). It is user's responsibility to make sure
        /// all the calls on this channel have finished (successfully or with an error)
        /// before shutting down the channel to ensure channel shutdown won't impact
        /// the outcome of those remote calls.
        /// </remarks>
        public Task ShutdownAsync()
        {
            return ShutdownAsyncCore();
        }

        /// <summary>Provides implementation of a non-virtual public member.</summary>
        #pragma warning disable 1998
        protected virtual async Task ShutdownAsyncCore()
        {
            // default implementation is no-op for backwards compatibility, but all implementations
            // are expected to override this method.

            // warning 1998 is disabled to avoid needing TaskUtils.CompletedTask, which is
            // only available in Grpc.Core
        }
        #pragma warning restore 1998
    }
}
