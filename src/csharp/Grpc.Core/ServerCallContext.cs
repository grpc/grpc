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
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core
{
    /// <summary>
    /// Context for a server-side call.
    /// </summary>
    public sealed class ServerCallContext
    {
        // TODO(jtattermusch): expose method to send initial metadata back to client

        private readonly string method;
        private readonly string host;
        private readonly string peer;
        private readonly DateTime deadline;
        private readonly Metadata requestHeaders;
        private readonly CancellationToken cancellationToken;
        private readonly Metadata responseTrailers = new Metadata();

        private Status status = Status.DefaultSuccess;

        public ServerCallContext(string method, string host, string peer, DateTime deadline, Metadata requestHeaders, CancellationToken cancellationToken)
        {
            this.method = method;
            this.host = host;
            this.peer = peer;
            this.deadline = deadline;
            this.requestHeaders = requestHeaders;
            this.cancellationToken = cancellationToken;
        }
            
        /// <summary> Name of method called in this RPC. </summary>
        public string Method
        {
            get
            {
                return this.method;
            }
        }

        /// <summary> Name of host called in this RPC. </summary>
        public string Host
        {
            get
            {
                return this.host;
            }
        }

        /// <summary> Address of the remote endpoint in URI format. </summary>
        public string Peer
        {
            get
            {
                return this.peer;
            }
        }

        /// <summary> Deadline for this RPC. </summary>
        public DateTime Deadline
        {
            get
            {
                return this.deadline;
            }
        }

        /// <summary> Initial metadata sent by client. </summary>
        public Metadata RequestHeaders
        {
            get
            {
                return this.requestHeaders;
            }
        }

        // TODO(jtattermusch): support signalling cancellation.
        /// <summary> Cancellation token signals when call is cancelled. </summary>
        public CancellationToken CancellationToken
        {
            get
            {
                return this.cancellationToken;
            }
        }

        /// <summary> Trailers to send back to client after RPC finishes.</summary>
        public Metadata ResponseTrailers
        {
            get
            {
                return this.responseTrailers;
            }
        }

        /// <summary> Status to send back to client after RPC finishes.</summary>
        public Status Status
        {
            get
            {
                return this.status;
            }

            set
            {
                status = value;
            }
        }
    }
}
