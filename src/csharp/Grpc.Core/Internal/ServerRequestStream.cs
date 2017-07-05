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
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    internal class ServerRequestStream<TRequest, TResponse> : IAsyncStreamReader<TRequest>
        where TRequest : class
        where TResponse : class
    {
        readonly AsyncCallServer<TRequest, TResponse> call;
        TRequest current;

        public ServerRequestStream(AsyncCallServer<TRequest, TResponse> call)
        {
            this.call = call;
        }

        public TRequest Current
        {
            get
            {
                if (current == null)
                {
                    throw new InvalidOperationException("No current element is available.");
                }
                return current;
            }
        }

        public async Task<bool> MoveNext(CancellationToken token)
        {
            if (token != CancellationToken.None)
            {
                throw new InvalidOperationException("Cancellation of individual reads is not supported.");
            }
            var result = await call.ReadMessageAsync().ConfigureAwait(false);
            this.current = result;
            return result != null;
        }

        public void Dispose()
        {
            // TODO(jtattermusch): implement the semantics of stream disposal.
        }
    }
}
