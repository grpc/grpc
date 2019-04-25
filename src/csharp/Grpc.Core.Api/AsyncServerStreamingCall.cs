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

namespace Grpc.Core
{
    /// <summary>
    /// Return type for server streaming calls.
    /// </summary>
    /// <typeparam name="TResponse">Response message type for this call.</typeparam>
    public sealed class AsyncServerStreamingCall<TResponse> : IDisposable
    {
        readonly IAsyncStreamReader<TResponse> responseStream;
        readonly Task<Metadata> responseHeadersAsync;
        readonly Func<Status> getStatusFunc;
        readonly Func<Metadata> getTrailersFunc;
        readonly Action disposeAction;

        /// <summary>
        /// Creates a new AsyncDuplexStreamingCall object with the specified properties.
        /// </summary>
        /// <param name="responseStream">Stream of response values.</param>
        /// <param name="responseHeadersAsync">Response headers of the asynchronous call.</param>
        /// <param name="getStatusFunc">Delegate returning the status of the call.</param>
        /// <param name="getTrailersFunc">Delegate returning the trailing metadata of the call.</param>
        /// <param name="disposeAction">Delegate to invoke when Dispose is called on the call object.</param>
        public AsyncServerStreamingCall(IAsyncStreamReader<TResponse> responseStream,
                                        Task<Metadata> responseHeadersAsync,
                                        Func<Status> getStatusFunc,
                                        Func<Metadata> getTrailersFunc,
                                        Action disposeAction)
        {
            this.responseStream = responseStream;
            this.responseHeadersAsync = responseHeadersAsync;
            this.getStatusFunc = getStatusFunc;
            this.getTrailersFunc = getTrailersFunc;
            this.disposeAction = disposeAction;
        }

        /// <summary>
        /// Async stream to read streaming responses.
        /// </summary>
        public IAsyncStreamReader<TResponse> ResponseStream
        {
            get
            {
                return responseStream;
            }
        }

        /// <summary>
        /// Asynchronous access to response headers.
        /// </summary>
        public Task<Metadata> ResponseHeadersAsync
        {
            get
            {
                return this.responseHeadersAsync;
            }
        }

        /// <summary>
        /// Gets the call status if the call has already finished.
        /// Throws InvalidOperationException otherwise.
        /// </summary>
        public Status GetStatus()
        {
            return getStatusFunc();
        }

        /// <summary>
        /// Gets the call trailing metadata if the call has already finished.
        /// Throws InvalidOperationException otherwise.
        /// </summary>
        public Metadata GetTrailers()
        {
            return getTrailersFunc();
        }

        /// <summary>
        /// Provides means to cleanup after the call.
        /// If the call has already finished normally (response stream has been fully read), doesn't do anything.
        /// Otherwise, requests cancellation of the call which should terminate all pending async operations associated with the call.
        /// As a result, all resources being used by the call should be released eventually.
        /// </summary>
        /// <remarks>
        /// Normally, there is no need for you to dispose the call unless you want to utilize the
        /// "Cancel" semantics of invoking <c>Dispose</c>.
        /// </remarks>
        public void Dispose()
        {
            disposeAction.Invoke();
        }
    }
}
