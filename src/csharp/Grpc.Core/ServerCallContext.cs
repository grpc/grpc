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
using System.Threading.Tasks;

using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// Context for a server-side call.
    /// </summary>
    public class ServerCallContext
    {
        private readonly CallSafeHandle callHandle;
        private readonly string method;
        private readonly string host;
        private readonly DateTime deadline;
        private readonly Metadata requestHeaders;
        private readonly CancellationToken cancellationToken;
        private readonly Metadata responseTrailers = new Metadata();

        private Status status = Status.DefaultSuccess;
        private Func<Metadata, Task> writeHeadersFunc;
        private IHasWriteOptions writeOptionsHolder;
        private Lazy<AuthContext> authContext;

        internal ServerCallContext(CallSafeHandle callHandle, string method, string host, DateTime deadline, Metadata requestHeaders, CancellationToken cancellationToken,
            Func<Metadata, Task> writeHeadersFunc, IHasWriteOptions writeOptionsHolder)
        {
            this.callHandle = callHandle;
            this.method = method;
            this.host = host;
            this.deadline = deadline;
            this.requestHeaders = requestHeaders;
            this.cancellationToken = cancellationToken;
            this.writeHeadersFunc = writeHeadersFunc;
            this.writeOptionsHolder = writeOptionsHolder;
            this.authContext = new Lazy<AuthContext>(GetAuthContextEager);
        }

        /// <summary>
        /// Asynchronously sends response headers for the current call to the client. This method may only be invoked once for each call and needs to be invoked
        /// before any response messages are written. Writing the first response message implicitly sends empty response headers if <c>WriteResponseHeadersAsync</c> haven't
        /// been called yet.
        /// </summary>
        /// <param name="responseHeaders">The response headers to send.</param>
        /// <returns>The task that finished once response headers have been written.</returns>
        public Task WriteResponseHeadersAsync(Metadata responseHeaders)
        {
            return writeHeadersFunc(responseHeaders);
        }

        /// <summary>
        /// Creates a propagation token to be used to propagate call context to a child call.
        /// </summary>
        public ContextPropagationToken CreatePropagationToken(ContextPropagationOptions options = null)
        {
            return new ContextPropagationToken(callHandle, deadline, cancellationToken, options);
        }
            
        /// <summary>Name of method called in this RPC.</summary>
        public string Method
        {
            get
            {
                return this.method;
            }
        }

        /// <summary>Name of host called in this RPC.</summary>
        public string Host
        {
            get
            {
                return this.host;
            }
        }

        /// <summary>Address of the remote endpoint in URI format.</summary>
        public string Peer
        {
            get
            {
                // Getting the peer lazily is fine as the native call is guaranteed
                // not to be disposed before user-supplied server side handler returns.
                // Most users won't need to read this field anyway.
                return this.callHandle.GetPeer();
            }
        }

        /// <summary>Deadline for this RPC.</summary>
        public DateTime Deadline
        {
            get
            {
                return this.deadline;
            }
        }

        /// <summary>Initial metadata sent by client.</summary>
        public Metadata RequestHeaders
        {
            get
            {
                return this.requestHeaders;
            }
        }

        /// <summary>Cancellation token signals when call is cancelled.</summary>
        public CancellationToken CancellationToken
        {
            get
            {
                return this.cancellationToken;
            }
        }

        /// <summary>Trailers to send back to client after RPC finishes.</summary>
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

        /// <summary>
        /// Allows setting write options for the following write.
        /// For streaming response calls, this property is also exposed as on IServerStreamWriter for convenience.
        /// Both properties are backed by the same underlying value.
        /// </summary>
        public WriteOptions WriteOptions
        {
            get
            {
                return writeOptionsHolder.WriteOptions;
            }

            set
            {
                writeOptionsHolder.WriteOptions = value;
            }
        }

        /// <summary>
        /// Gets the <c>AuthContext</c> associated with this call.
        /// Note: Access to AuthContext is an experimental API that can change without any prior notice.
        /// </summary>
        public AuthContext AuthContext
        {
            get
            {
                return authContext.Value;
            }
        }

        private AuthContext GetAuthContextEager()
        {
            using (var authContextNative = callHandle.GetAuthContext())
            {
                return authContextNative.ToAuthContext();
            }
        }
    }

    /// <summary>
    /// Allows sharing write options between ServerCallContext and other objects.
    /// </summary>
    public interface IHasWriteOptions
    {
        /// <summary>
        /// Gets or sets the write options.
        /// </summary>
        WriteOptions WriteOptions { get; set; }
    }
}
