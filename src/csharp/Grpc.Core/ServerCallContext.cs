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
using System.Threading.Tasks;

namespace Grpc.Core
{
    /// <summary>
    /// Context for a server-side call.
    /// </summary>
    public abstract class ServerCallContext
    {
        /// <summary>
        /// Creates a new instance of <c>ServerCallContext</c>.
        /// </summary>
        protected ServerCallContext()
        {
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
            return WriteResponseHeadersInternalAsync(responseHeaders);
        }

        /// <summary>
        /// Creates a propagation token to be used to propagate call context to a child call.
        /// </summary>
        public ContextPropagationToken CreatePropagationToken(ContextPropagationOptions options = null)
        {
            return CreatePropagationTokenInternal(options);
        }

        /// <summary>Name of method called in this RPC.</summary>
        public string Method => MethodInternal;

        /// <summary>Name of host called in this RPC.</summary>
        public string Host => HostInternal;

        /// <summary>Address of the remote endpoint in URI format.</summary>
        public string Peer => PeerInternal;

        /// <summary>Deadline for this RPC.</summary>
        public DateTime Deadline => DeadlineInternal;

        /// <summary>Initial metadata sent by client.</summary>
        public Metadata RequestHeaders => RequestHeadersInternal;

        /// <summary>Cancellation token signals when call is cancelled.</summary>
        public CancellationToken CancellationToken => CancellationTokenInternal;

        /// <summary>Trailers to send back to client after RPC finishes.</summary>
        public Metadata ResponseTrailers => ResponseTrailersInternal;

        /// <summary> Status to send back to client after RPC finishes.</summary>
        public Status Status
        {
            get
            {
                return StatusInternal;
            }

            set
            {
                StatusInternal = value;
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
                return WriteOptionsInternal;
            }

            set
            {
                WriteOptionsInternal = value;
            }
        }

        /// <summary>
        /// Gets the <c>AuthContext</c> associated with this call.
        /// Note: Access to AuthContext is an experimental API that can change without any prior notice.
        /// </summary>
        public AuthContext AuthContext => AuthContextInternal;

        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract Task WriteResponseHeadersInternalAsync(Metadata responseHeaders);
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract ContextPropagationToken CreatePropagationTokenInternal(ContextPropagationOptions options);
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract string MethodInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract string HostInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract string PeerInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract DateTime DeadlineInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract Metadata RequestHeadersInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract CancellationToken CancellationTokenInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract Metadata ResponseTrailersInternal { get; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract Status StatusInternal { get; set; }
        /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract WriteOptions WriteOptionsInternal { get; set; }
          /// <summary>Provides implementation of a non-virtual public member.</summary>
        protected abstract AuthContext AuthContextInternal { get; }
    }
}
