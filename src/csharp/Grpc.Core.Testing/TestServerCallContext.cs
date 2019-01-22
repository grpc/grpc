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

namespace Grpc.Core.Testing
{
    /// <summary>
    /// Creates test doubles for <c>ServerCallContext</c>.
    /// </summary>
    public static class TestServerCallContext
    {
        /// <summary>
        /// Creates a test double for <c>ServerCallContext</c>. Only for testing.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public static ServerCallContext Create(string method, string host, DateTime deadline, Metadata requestHeaders, CancellationToken cancellationToken,
            string peer, AuthContext authContext, ContextPropagationToken contextPropagationToken,
            Func<Metadata, Task> writeHeadersFunc, Func<WriteOptions> writeOptionsGetter, Action<WriteOptions> writeOptionsSetter)
        {
            return new TestingServerCallContext(method, host, deadline, requestHeaders, cancellationToken, peer,
                authContext, contextPropagationToken, writeHeadersFunc, writeOptionsGetter, writeOptionsSetter);
        }

        private class TestingServerCallContext : ServerCallContext
        {
            private readonly string method;
            private readonly string host;
            private readonly DateTime deadline;
            private readonly Metadata requestHeaders;
            private readonly CancellationToken cancellationToken;
            private readonly Metadata responseTrailers = new Metadata();
            private Status status;
            private readonly string peer;
            private readonly AuthContext authContext;
            private readonly ContextPropagationToken contextPropagationToken;
            private readonly Func<Metadata, Task> writeHeadersFunc;
            private readonly Func<WriteOptions> writeOptionsGetter;
            private readonly Action<WriteOptions> writeOptionsSetter;

            public TestingServerCallContext(string method, string host, DateTime deadline, Metadata requestHeaders, CancellationToken cancellationToken,
                string peer, AuthContext authContext, ContextPropagationToken contextPropagationToken,
                Func<Metadata, Task> writeHeadersFunc, Func<WriteOptions> writeOptionsGetter, Action<WriteOptions> writeOptionsSetter)
            {
                this.method = method;
                this.host = host;
                this.deadline = deadline;
                this.requestHeaders = requestHeaders;
                this.cancellationToken = cancellationToken;
                this.responseTrailers = new Metadata();
                this.status = Status.DefaultSuccess;
                this.peer = peer;
                this.authContext = authContext;
                this.contextPropagationToken = contextPropagationToken;
                this.writeHeadersFunc = writeHeadersFunc;
                this.writeOptionsGetter = writeOptionsGetter;
                this.writeOptionsSetter = writeOptionsSetter;
            }

            protected override string MethodCore => method;

            protected override string HostCore => host;

            protected override string PeerCore => peer;

            protected override DateTime DeadlineCore => deadline;

            protected override Metadata RequestHeadersCore => requestHeaders;

            protected override CancellationToken CancellationTokenCore => cancellationToken;

            protected override Metadata ResponseTrailersCore => responseTrailers;

            protected override Status StatusCore { get => status; set => status = value; }
            protected override WriteOptions WriteOptionsCore { get => writeOptionsGetter(); set => writeOptionsSetter(value); }

            protected override AuthContext AuthContextCore => authContext;

            protected override ContextPropagationToken CreatePropagationTokenCore(ContextPropagationOptions options)
            {
                return contextPropagationToken;
            }

            protected override Task WriteResponseHeadersAsyncCore(Metadata responseHeaders)
            {
                return writeHeadersFunc(responseHeaders);
            }
        }
    }
}
