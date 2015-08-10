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
    /// Options for calls made by client.
    /// </summary>
    public class CallOptions
    {
        readonly Metadata headers;
        readonly DateTime deadline;
        readonly CancellationToken cancellationToken;
        readonly WriteOptions writeOptions;
        readonly ContextPropagationToken propagationToken;

        /// <summary>
        /// Creates a new instance of <c>CallOptions</c>.
        /// </summary>
        /// <param name="headers">Headers to be sent with the call.</param>
        /// <param name="deadline">Deadline for the call to finish. null means no deadline.</param>
        /// <param name="cancellationToken">Can be used to request cancellation of the call.</param>
        /// <param name="writeOptions">Write options that will be used for this call.</param>
        /// <param name="propagationToken">Context propagation token obtained from <see cref="ServerCallContext"/>.</param>
        public CallOptions(Metadata headers = null, DateTime? deadline = null, CancellationToken? cancellationToken = null,
                           WriteOptions writeOptions = null, ContextPropagationToken propagationToken = null)
        {
            // TODO(jtattermusch): consider only creating metadata object once it's really needed.
            this.headers = headers ?? new Metadata();
            this.deadline = deadline ?? (propagationToken != null ? propagationToken.Deadline : DateTime.MaxValue);
            this.cancellationToken = cancellationToken ?? (propagationToken != null ? propagationToken.CancellationToken : CancellationToken.None);
            this.writeOptions = writeOptions;
            this.propagationToken = propagationToken;
        }

        /// <summary>
        /// Headers to send at the beginning of the call.
        /// </summary>
        public Metadata Headers
        {
            get { return headers; }
        }

        /// <summary>
        /// Call deadline.
        /// </summary>
        public DateTime Deadline
        {
            get { return deadline; }
        }

        /// <summary>
        /// Token that can be used for cancelling the call.
        /// </summary>
        public CancellationToken CancellationToken
        {
            get { return cancellationToken; }
        }

        /// <summary>
        /// Write options that will be used for this call.
        /// </summary>
        public WriteOptions WriteOptions
        {
            get
            {
                return this.writeOptions;
            }
        }

        /// <summary>
        /// Token for propagating parent call context.
        /// </summary>
        public ContextPropagationToken PropagationToken
        {
            get
            {
                return this.propagationToken;
            }
        }
    }
}
