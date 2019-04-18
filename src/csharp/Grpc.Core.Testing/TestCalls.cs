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
        public static AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(
            IClientStreamWriter<TRequest> requestStream, IAsyncStreamReader<TResponse> responseStream,
            Task<Metadata> responseHeadersAsync, Func<Status> getStatusFunc,
            Func<Metadata> getTrailersFunc, Action disposeAction)
        {
            return new AsyncDuplexStreamingCall<TRequest, TResponse>(requestStream, responseStream, responseHeadersAsync, getStatusFunc, getTrailersFunc, disposeAction);
        }
    }
}
