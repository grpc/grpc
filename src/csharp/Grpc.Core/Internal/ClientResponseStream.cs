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
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    internal class ClientResponseStream<TRequest, TResponse> : IAsyncStreamReader<TResponse>
        where TRequest : class
        where TResponse : class
    {
        readonly AsyncCall<TRequest, TResponse> call;
        TResponse current;

        public ClientResponseStream(AsyncCall<TRequest, TResponse> call)
        {
            this.call = call;
        }

        public TResponse Current
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

            if (result == null)
            {
                await call.StreamingResponseCallFinishedTask.ConfigureAwait(false);
                return false;
            }
            return true;
        }

        public void Dispose()
        {
            // TODO(jtattermusch): implement the semantics of stream disposal.
        }
    }
}
