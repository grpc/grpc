#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
    /// C core provides some other contexts (like tracing context) that
    /// are not accessible to C# layer, but this token still allows propagating them.
    /// </summary>
    public class ContextPropagationToken
    {
        /// <summary>
        /// Default propagation mask used by C core.
        /// </summary>
        const ContextPropagationFlags DefaultCoreMask = (ContextPropagationFlags) 0xffff;

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
            this.parentCall = Preconditions.CheckNotNull(parentCall);
            this.deadline = deadline;
            this.cancellationToken = cancellationToken;
            this.options = options ?? ContextPropagationOptions.Default;
        }

        internal CallSafeHandle ParentCall
        {
            get
            {
                return this.parentCall;
            }
        }

        internal DateTime Deadline
        {
            get
            {
                return this.deadline;
            }
        }

        internal CancellationToken CancellationToken
        {
            get
            {
                return this.cancellationToken;
            }
        }

        internal ContextPropagationOptions Options
        {
            get
            {
                return this.options;
            }
        }

        internal bool IsPropagateDeadline
        {
            get { return false; }
        }

        internal bool IsPropagateCancellation
        {
            get { return false; }
        }
    }

    /// <summary>
    /// Options for <see cref="ContextPropagationToken"/>.
    /// </summary>
    public class ContextPropagationOptions
    {
        public static readonly ContextPropagationOptions Default = new ContextPropagationOptions();
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
