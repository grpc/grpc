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
using Grpc.Core;

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
            return new ServerCallContext(null, method, host, deadline, requestHeaders, cancellationToken,
                writeHeadersFunc, new WriteOptionsHolder(writeOptionsGetter, writeOptionsSetter),
                () => peer, () => authContext, () => contextPropagationToken);
        }

        private class WriteOptionsHolder : IHasWriteOptions
        {
            Func<WriteOptions> writeOptionsGetter;
            Action<WriteOptions> writeOptionsSetter;

            public WriteOptionsHolder(Func<WriteOptions> writeOptionsGetter, Action<WriteOptions> writeOptionsSetter)
            {
                this.writeOptionsGetter = writeOptionsGetter;
                this.writeOptionsSetter = writeOptionsSetter;
            }

            public WriteOptions WriteOptions { get => writeOptionsGetter(); set => writeOptionsSetter(value); }
        }
    }
}
