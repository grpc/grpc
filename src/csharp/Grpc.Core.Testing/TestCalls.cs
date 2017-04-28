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
using System.Threading.Tasks;
using Grpc.Core;

namespace Grpc.Core.Testing
{
    /// <summary>
    /// Test doubles for client-side call objects.
    /// </summary>
    public static class TestCalls
    {
        /// <summary>
        /// Creates a test double for <c>AsyncUnaryCall</c>. Only for testing.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public static AsyncUnaryCall<TResponse> AsyncUnaryCall<TResponse> (
            Task<TResponse> responseAsync, Task<Metadata> responseHeadersAsync, Func<Status> getStatusFunc,
            Func<Metadata> getTrailersFunc, Action disposeAction)
        {
            return new AsyncUnaryCall<TResponse>(responseAsync, responseHeadersAsync, getStatusFunc, getTrailersFunc, disposeAction);
        }

        /// <summary>
        /// Creates a test double for <c>AsyncClientStreamingCall</c>. Only for testing.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public static AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(
            IClientStreamWriter<TRequest> requestStream, Task<TResponse> responseAsync,
            Task<Metadata> responseHeadersAsync, Func<Status> getStatusFunc,
            Func<Metadata> getTrailersFunc, Action disposeAction)
        {
            return new AsyncClientStreamingCall<TRequest, TResponse>(requestStream, responseAsync, responseHeadersAsync, getStatusFunc, getTrailersFunc, disposeAction);
        }

        /// <summary>
        /// Creates a test double for <c>AsyncServerStreamingCall</c>. Only for testing.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public static AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TResponse>(
            IAsyncStreamReader<TResponse> responseStream, Task<Metadata> responseHeadersAsync, 
            Func<Status> getStatusFunc, Func<Metadata> getTrailersFunc, Action disposeAction)
        {
            return new AsyncServerStreamingCall<TResponse>(responseStream, responseHeadersAsync, getStatusFunc, getTrailersFunc, disposeAction);
        }

        /// <summary>
        /// Creates a test double for <c>AsyncDuplexStreamingCall</c>. Only for testing.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public static AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TResponse, TRequest>(
            IClientStreamWriter<TRequest> requestStream, IAsyncStreamReader<TResponse> responseStream,
            Task<Metadata> responseHeadersAsync, Func<Status> getStatusFunc,
            Func<Metadata> getTrailersFunc, Action disposeAction)
        {
            return new AsyncDuplexStreamingCall<TRequest, TResponse>(requestStream, responseStream, responseHeadersAsync, getStatusFunc, getTrailersFunc, disposeAction);
        }
    }
}
