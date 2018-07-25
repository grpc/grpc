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
        private readonly Func<Metadata, Task> writeHeadersFunc;
        private readonly IHasWriteOptions writeOptionsHolder;
        private readonly Lazy<AuthContext> authContext;
        private readonly Func<string> testingOnlyPeerGetter;
        private readonly Func<AuthContext> testingOnlyAuthContextGetter;
        private readonly Func<ContextPropagationToken> testingOnlyContextPropagationTokenFactory;

        private Status status = Status.DefaultSuccess;

        internal ServerCallContext(CallSafeHandle callHandle, string method, string host, DateTime deadline, Metadata requestHeaders, CancellationToken cancellationToken,
            Func<Metadata, Task> writeHeadersFunc, IHasWriteOptions writeOptionsHolder)
            : this(callHandle, method, host, deadline, requestHeaders, cancellationToken, writeHeadersFunc, writeOptionsHolder, null, null, null)
        {
        }

        // Additional constructor params should be used for testing only
        internal ServerCallContext(CallSafeHandle callHandle, string method, string host, DateTime deadline, Metadata requestHeaders, CancellationToken cancellationToken,
            Func<Metadata, Task> writeHeadersFunc, IHasWriteOptions writeOptionsHolder,
            Func<string> testingOnlyPeerGetter, Func<AuthContext> testingOnlyAuthContextGetter, Func<ContextPropagationToken> testingOnlyContextPropagationTokenFactory)
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
            this.testingOnlyPeerGetter = testingOnlyPeerGetter;
            this.testingOnlyAuthContextGetter = testingOnlyAuthContextGetter;
            this.testingOnlyContextPropagationTokenFactory = testingOnlyContextPropagationTokenFactory;
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
            if (testingOnlyContextPropagationTokenFactory != null)
            {
                return testingOnlyContextPropagationTokenFactory();
            }
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
                if (testingOnlyPeerGetter != null)
                {
                    return testingOnlyPeerGetter();
                }
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
                if (testingOnlyAuthContextGetter != null)
                {
                    return testingOnlyAuthContextGetter();
                }
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
    internal interface IHasWriteOptions
    {
        /// <summary>
        /// Gets or sets the write options.
        /// </summary>
        WriteOptions WriteOptions { get; set; }
    }
}
