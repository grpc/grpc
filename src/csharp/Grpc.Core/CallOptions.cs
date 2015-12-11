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
    public struct CallOptions
    {
        Metadata headers;
        DateTime? deadline;
        CancellationToken cancellationToken;
        WriteOptions writeOptions;
        ContextPropagationToken propagationToken;
        CallCredentials credentials;

        /// <summary>
        /// Creates a new instance of <c>CallOptions</c> struct.
        /// </summary>
        /// <param name="headers">Headers to be sent with the call.</param>
        /// <param name="deadline">Deadline for the call to finish. null means no deadline.</param>
        /// <param name="cancellationToken">Can be used to request cancellation of the call.</param>
        /// <param name="writeOptions">Write options that will be used for this call.</param>
        /// <param name="propagationToken">Context propagation token obtained from <see cref="ServerCallContext"/>.</param>
        /// <param name="credentials">Credentials to use for this call.</param>
        public CallOptions(Metadata headers = null, DateTime? deadline = null, CancellationToken cancellationToken = default(CancellationToken),
                           WriteOptions writeOptions = null, ContextPropagationToken propagationToken = null, CallCredentials credentials = null)
        {
            this.headers = headers;
            this.deadline = deadline;
            this.cancellationToken = cancellationToken;
            this.writeOptions = writeOptions;
            this.propagationToken = propagationToken;
            this.credentials = credentials;
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
        public DateTime? Deadline
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

        /// <summary>
        /// Credentials to use for this call.
        /// </summary>
        public CallCredentials Credentials
        {
            get
            {
                return this.credentials;
            }
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>Headers</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="headers">The headers.</param>
        public CallOptions WithHeaders(Metadata headers)
        {
            var newOptions = this;
            newOptions.headers = headers;
            return newOptions;
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>Deadline</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="deadline">The deadline.</param>
        public CallOptions WithDeadline(DateTime deadline)
        {
            var newOptions = this;
            newOptions.deadline = deadline;
            return newOptions;
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>CancellationToken</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="cancellationToken">The cancellation token.</param>
        public CallOptions WithCancellationToken(CancellationToken cancellationToken)
        {
            var newOptions = this;
            newOptions.cancellationToken = cancellationToken;
            return newOptions;
        }

        /// <summary>
        /// Returns a new instance of <see cref="CallOptions"/> with 
        /// all previously unset values set to their defaults and deadline and cancellation
        /// token propagated when appropriate.
        /// </summary>
        internal CallOptions Normalize()
        {
            var newOptions = this;
            if (propagationToken != null)
            {
                if (propagationToken.Options.IsPropagateDeadline)
                {
                    Preconditions.CheckArgument(!newOptions.deadline.HasValue,
                        "Cannot propagate deadline from parent call. The deadline has already been set explicitly.");
                    newOptions.deadline = propagationToken.ParentDeadline;
                }
                if (propagationToken.Options.IsPropagateCancellation)
                {
                    Preconditions.CheckArgument(!newOptions.cancellationToken.CanBeCanceled,
                        "Cannot propagate cancellation token from parent call. The cancellation token has already been set to a non-default value.");
                    newOptions.cancellationToken = propagationToken.ParentCancellationToken;
                }
            }
                
            newOptions.headers = newOptions.headers ?? Metadata.Empty;
            newOptions.deadline = newOptions.deadline ?? DateTime.MaxValue;
            return newOptions;
        }
    }
}
