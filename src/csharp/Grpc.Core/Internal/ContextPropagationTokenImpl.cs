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
using System.Threading;

using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Implementation of <c>ContextPropagationToken</c> that carries
    /// all fields needed for context propagation by C-core based implementation of gRPC.
    /// Instances of <c>ContextPropagationToken</c> that are not of this
    /// type will be recognized as "foreign" and will be silently ignored
    /// (treated as if null).
    /// </summary>
    internal class ContextPropagationTokenImpl : ContextPropagationToken
    {
        /// <summary>
        /// Default propagation mask used by C core.
        /// </summary>
        private const ContextPropagationFlags DefaultCoreMask = (ContextPropagationFlags)0xffff;

        /// <summary>
        /// Default propagation mask used by C# - we want to propagate deadline 
        /// and cancellation token by our own means, everything else will be propagated
        /// by C core automatically (according to <c>DefaultCoreMask</c>).
        /// </summary>
        internal const ContextPropagationFlags DefaultMask = DefaultCoreMask
            & ~ContextPropagationFlags.Deadline & ~ContextPropagationFlags.Cancellation;

        readonly CallSafeHandle parentCall;
        readonly DateTime deadline;
        readonly CancellationToken cancellationToken;
        readonly ContextPropagationOptions options;

        internal ContextPropagationTokenImpl(CallSafeHandle parentCall, DateTime deadline, CancellationToken cancellationToken, ContextPropagationOptions options)
        {
            this.parentCall = GrpcPreconditions.CheckNotNull(parentCall);
            this.deadline = deadline;
            this.cancellationToken = cancellationToken;
            this.options = options ?? ContextPropagationOptions.Default;
        }

        /// <summary>
        /// Gets the native handle of the parent call.
        /// </summary>
        internal CallSafeHandle ParentCall
        {
            get
            {
                return this.parentCall;
            }
        }

        /// <summary>
        /// Gets the parent call's deadline.
        /// </summary>
        internal DateTime ParentDeadline
        {
            get
            {
                return this.deadline;
            }
        }

        /// <summary>
        /// Gets the parent call's cancellation token.
        /// </summary>
        internal CancellationToken ParentCancellationToken
        {
            get
            {
                return this.cancellationToken;
            }
        }

        /// <summary>
        /// Get the context propagation options.
        /// </summary>
        internal ContextPropagationOptions Options
        {
            get
            {
                return this.options;
            }
        }
    }

    internal static class ContextPropagationTokenExtensions
    {
        /// <summary>
        /// Converts given <c>ContextPropagationToken</c> to <c>ContextPropagationTokenImpl</c>
        /// if possible or returns null.
        /// Being able to convert means that the context propagation token is recognized as
        /// "ours" (was created by this implementation).
        /// </summary>
        public static ContextPropagationTokenImpl AsImplOrNull(this ContextPropagationToken instanceOrNull)
        {
            return instanceOrNull as ContextPropagationTokenImpl;
        }
    }
}
