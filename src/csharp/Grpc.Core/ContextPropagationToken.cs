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
using System.Threading;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Token for propagating context of server side handlers to child calls.
    /// In situations when a backend is making calls to another backend,
    /// it makes sense to propagate properties like deadline and cancellation 
    /// token of the server call to the child call.
    /// The gRPC native layer provides some other contexts (like tracing context) that
    /// are not accessible to explicitly C# layer, but this token still allows propagating them.
    /// </summary>
    public class ContextPropagationToken
    {
        /// <summary>
        /// Default propagation mask used by C core.
        /// </summary>
        private const ContextPropagationFlags DefaultCoreMask = (ContextPropagationFlags)0xffff;

        /// <summary>
        /// Default propagation mask used by C# - we want to propagate deadline 
        /// and cancellation token by our own means.
        /// </summary>
        internal const ContextPropagationFlags DefaultMask = DefaultCoreMask
            & ~ContextPropagationFlags.Deadline & ~ContextPropagationFlags.Cancellation;

        readonly CallSafeHandle parentCall;
        readonly DateTime deadline;
        readonly CancellationToken cancellationToken;
        readonly ContextPropagationOptions options;

        internal ContextPropagationToken(CallSafeHandle parentCall, DateTime deadline, CancellationToken cancellationToken, ContextPropagationOptions options)
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

    /// <summary>
    /// Options for <see cref="ContextPropagationToken"/>.
    /// </summary>
    public class ContextPropagationOptions
    {
        /// <summary>
        /// The context propagation options that will be used by default.
        /// </summary>
        public static readonly ContextPropagationOptions Default = new ContextPropagationOptions();

        bool propagateDeadline;
        bool propagateCancellation;

        /// <summary>
        /// Creates new context propagation options.
        /// </summary>
        /// <param name="propagateDeadline">If set to <c>true</c> parent call's deadline will be propagated to the child call.</param>
        /// <param name="propagateCancellation">If set to <c>true</c> parent call's cancellation token will be propagated to the child call.</param>
        public ContextPropagationOptions(bool propagateDeadline = true, bool propagateCancellation = true)
        {
            this.propagateDeadline = propagateDeadline;
            this.propagateCancellation = propagateCancellation;
        }
            
        /// <summary><c>true</c> if parent call's deadline should be propagated to the child call.</summary>
        public bool IsPropagateDeadline
        {
            get { return this.propagateDeadline; }
        }

        /// <summary><c>true</c> if parent call's cancellation token should be propagated to the child call.</summary>
        public bool IsPropagateCancellation
        {
            get { return this.propagateCancellation; }
        }
    }

    /// <summary>
    /// Context propagation flags from grpc/grpc.h.
    /// </summary>
    [Flags]
    internal enum ContextPropagationFlags
    {
        Deadline = 1,
        CensusStatsContext = 2,
        CensusTracingContext = 4,
        Cancellation = 8
    }
}
