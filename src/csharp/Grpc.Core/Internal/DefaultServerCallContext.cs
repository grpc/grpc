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
using System.Threading.Tasks;

using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// Default implementation of <c>ServerCallContext</c>.
    /// </summary>
    internal class DefaultServerCallContext : ServerCallContext
    {
        private readonly CallSafeHandle callHandle;
        private readonly string method;
        private readonly string host;
        private readonly DateTime deadline;
        private readonly Metadata requestHeaders;
        private readonly CancellationToken cancellationToken;
        private readonly Metadata responseTrailers;
        private Status status;
        private readonly IServerResponseStream serverResponseStream;
        private AuthContext lazyAuthContext;

        /// <summary>
        /// Creates a new instance of <c>ServerCallContext</c>.
        /// To allow reuse of ServerCallContext API by different gRPC implementations, the implementation of some members is provided externally.
        /// To provide state, this <c>ServerCallContext</c> instance and <c>extraData</c> will be passed to the member implementations.
        /// </summary>
        internal DefaultServerCallContext(CallSafeHandle callHandle, string method, string host, DateTime deadline,
            Metadata requestHeaders, CancellationToken cancellationToken, IServerResponseStream serverResponseStream)
        {
            this.callHandle = callHandle;
            this.method = method;
            this.host = host;
            this.deadline = deadline;
            this.requestHeaders = requestHeaders;
            this.cancellationToken = cancellationToken;
            this.responseTrailers = new Metadata();
            this.status = Status.DefaultSuccess;
            this.serverResponseStream = serverResponseStream;
        }

        protected override ContextPropagationToken CreatePropagationTokenCore(ContextPropagationOptions options)
        {
            return new ContextPropagationTokenImpl(callHandle, deadline, cancellationToken, options);
        }

        protected override Task WriteResponseHeadersAsyncCore(Metadata responseHeaders)
        {
            return serverResponseStream.WriteResponseHeadersAsync(responseHeaders);
        }

        protected override string MethodCore => method;

        protected override string HostCore => host;

        protected override string PeerCore => callHandle.GetPeer();

        protected override DateTime DeadlineCore => deadline;

        protected override Metadata RequestHeadersCore => requestHeaders;

        protected override CancellationToken CancellationTokenCore => cancellationToken;

        protected override Metadata ResponseTrailersCore => responseTrailers;

        protected override Status StatusCore
        {
            get => status;
            set => status = value;
        }

        protected override WriteOptions WriteOptionsCore
        {
            get => serverResponseStream.WriteOptions;
            set => serverResponseStream.WriteOptions = value;
        }

        protected override AuthContext AuthContextCore => lazyAuthContext ?? (lazyAuthContext = GetAuthContextEager());

        private AuthContext GetAuthContextEager()
        {
            using (var authContextNative = callHandle.GetAuthContext())
            {
                return authContextNative.ToAuthContext();
            }
        }
    }
}
