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
        CallFlags flags;

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
            this.flags = default(CallFlags);
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
        /// Token that can be used for cancelling the call on the client side.
        /// Cancelling the token will request cancellation
        /// of the remote call. Best effort will be made to deliver the cancellation
        /// notification to the server and interaction of the call with the server side
        /// will be terminated. Unless the call finishes before the cancellation could
        /// happen (there is an inherent race),
        /// the call will finish with <c>StatusCode.Cancelled</c> status.
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
            get { return this.writeOptions; }
        }

        /// <summary>
        /// Token for propagating parent call context.
        /// </summary>
        public ContextPropagationToken PropagationToken
        {
            get { return this.propagationToken; }
        }

        /// <summary>
        /// Credentials to use for this call.
        /// </summary>
        public CallCredentials Credentials
        {
            get { return this.credentials; }
        }

        /// <summary>
        /// If <c>true</c> and channel is in <c>ChannelState.TransientFailure</c>, the call will attempt waiting for the channel to recover
        /// instead of failing immediately (which is the default "FailFast" semantics).
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public bool IsWaitForReady
        {
            get { return (this.flags & CallFlags.WaitForReady) == CallFlags.WaitForReady; }
        }

        /// <summary>
        /// Flags to use for this call.
        /// </summary>
        internal CallFlags Flags
        {
            get { return this.flags; }
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
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>WriteOptions</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="writeOptions">The write options.</param>
        public CallOptions WithWriteOptions(WriteOptions writeOptions)
        {
            var newOptions = this;
            newOptions.writeOptions = writeOptions;
            return newOptions;
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>PropagationToken</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="propagationToken">The context propagation token.</param>
        public CallOptions WithPropagationToken(ContextPropagationToken propagationToken)
        {
            var newOptions = this;
            newOptions.propagationToken = propagationToken;
            return newOptions;
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>Credentials</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="credentials">The call credentials.</param>
        public CallOptions WithCredentials(CallCredentials credentials)
        {
            var newOptions = this;
            newOptions.credentials = credentials;
            return newOptions;
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with "WaitForReady" semantics enabled/disabled.
        /// <see cref="IsWaitForReady"/>.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public CallOptions WithWaitForReady(bool waitForReady = true)
        {
            if (waitForReady)
            {
                return WithFlags(this.flags | CallFlags.WaitForReady);
            }
            return WithFlags(this.flags & ~CallFlags.WaitForReady);
        }

        /// <summary>
        /// Returns new instance of <see cref="CallOptions"/> with
        /// <c>Flags</c> set to the value provided. Values of all other fields are preserved.
        /// </summary>
        /// <param name="flags">The call flags.</param>
        internal CallOptions WithFlags(CallFlags flags)
        {
            var newOptions = this;
            newOptions.flags = flags;
            return newOptions;
        }
    }
}
