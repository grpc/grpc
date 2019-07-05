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

namespace Grpc.Core.Internal
{
    internal struct ServerRpcState
    {
        private readonly Server server;
        private readonly CompletionQueueSafeHandle cq;
        private readonly Action<Server, CompletionQueueSafeHandle> continuation;

        public ServerRpcState(Server server, CompletionQueueSafeHandle cq, Action<Server, CompletionQueueSafeHandle> continuation)
        {
            this.server = server;
            this.cq = cq;
            this.continuation = continuation;
        }

        public CompletionQueueSafeHandle CompletionQueue
        {
            get { return cq; }
        }

        public void InvokeContinuation()
        {
            if (continuation != null) continuation(server, cq);
        }
        public void LogError(Exception ex)
        {
            if (server != null) server.LogError(ex);
        }
    }
}
