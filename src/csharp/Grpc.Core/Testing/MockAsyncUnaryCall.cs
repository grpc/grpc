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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Testing
{
    /// <summary>Allows mocking gRPC client objects by implementing the code-generated stub client interface <c>IFooClient</c></summary>
    public class MockAsyncUnaryCall<TResponse>
    {
        ClientSideStatus? clientSideStatus;
        TaskCompletionSource<Metadata> responseHeadersTcs = new TaskCompletionSource<Metadata>();
        TaskCompletionSource<TResponse> responseTcs = new TaskCompletionSource<TResponse>();
        AsyncUnaryCall<TResponse> call;

        /// <summary>
        /// Initializes a new instance of the <see cref="Grpc.Core.Testing.MockAsyncUnaryCall`1"/> class.
        /// </summary>
        public MockAsyncUnaryCall()
        {
            // TODO(jtattermusch): implement dispose action functionality
            this.call = new AsyncUnaryCall<TResponse>(responseTcs.Task, responseHeadersTcs.Task,
                () => clientSideStatus.Value.Status, () => clientSideStatus.Value.Trailers, () => {});
        }

        /// <summary>
        /// Sets the response for this mock call.
        /// </summary>
        public void SetResponse(TResponse response)
        {
            SetResponse(response, Status.DefaultSuccess, new Metadata(), new Metadata());
        }

        /// <summary>
        /// Sets the response for this mock call.
        /// </summary>
        public void SetResponse(TResponse response, Status status, Metadata responseHeaders, Metadata responseTrailers)
        {
            Preconditions.CheckNotNull(response, "response");
            Preconditions.CheckNotNull(responseHeaders, "responseHeaders");
            Preconditions.CheckNotNull(responseTrailers, "responseTrailers");

            responseHeadersTcs.SetResult(responseHeaders);
            clientSideStatus = new ClientSideStatus(status, responseTrailers);
            responseTcs.SetResult(response);
        }

        /// <summary>
        /// Returns the mock call object.
        /// </summary>
        public AsyncUnaryCall<TResponse> Call
        {
            get
            {
                return call;
            }
        }
    }
}
