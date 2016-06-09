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
using System.Runtime.CompilerServices;
using System.Threading.Tasks;

namespace Grpc.Core
{
    /// <summary>
    /// Return type for client streaming calls.
    /// </summary>
    /// <typeparam name="TRequest">Request message type for this call.</typeparam>
    /// <typeparam name="TResponse">Response message type for this call.</typeparam>
    public sealed class AsyncClientStreamingCall<TRequest, TResponse> : IDisposable
    {
        readonly IClientStreamWriter<TRequest> requestStream;
        readonly Task<TResponse> responseAsync;
        readonly Task<Metadata> responseHeadersAsync;
        readonly Func<Status> getStatusFunc;
        readonly Func<Metadata> getTrailersFunc;
        readonly Action disposeAction;

        internal AsyncClientStreamingCall(IClientStreamWriter<TRequest> requestStream, Task<TResponse> responseAsync, Task<Metadata> responseHeadersAsync, Func<Status> getStatusFunc, Func<Metadata> getTrailersFunc, Action disposeAction)
        {
            this.requestStream = requestStream;
            this.responseAsync = responseAsync;
            this.responseHeadersAsync = responseHeadersAsync;
            this.getStatusFunc = getStatusFunc;
            this.getTrailersFunc = getTrailersFunc;
            this.disposeAction = disposeAction;
        }

        /// <summary>
        /// Asynchronous call result.
        /// </summary>
        public Task<TResponse> ResponseAsync
        {
            get
            {
                return this.responseAsync;
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
        /// Async stream to send streaming requests.
        /// </summary>
        public IClientStreamWriter<TRequest> RequestStream
        {
            get
            {
                return requestStream;
            }
        }

        /// <summary>
        /// Allows awaiting this object directly.
        /// </summary>
        /// <returns></returns>
        public TaskAwaiter<TResponse> GetAwaiter()
        {
            return responseAsync.GetAwaiter();
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
        /// If the call has already finished normally (request stream has been completed and call result has been received), doesn't do anything.
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
