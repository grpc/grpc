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
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Handles server side native call lifecycle.
    /// </summary>
    internal class AsyncCallServer<TRequest, TResponse> : AsyncCallBase<TResponse, TRequest>
    {
        readonly CompletionCallbackDelegate finishedServersideHandler;
        readonly TaskCompletionSource<object> finishedServersideTcs = new TaskCompletionSource<object>();

        public AsyncCallServer(Func<TResponse, byte[]> serializer, Func<byte[], TRequest> deserializer) : base(serializer, deserializer)
        {
            this.finishedServersideHandler = CreateBatchCompletionCallback(HandleFinishedServerside);
        }

        public void Initialize(CallSafeHandle call)
        {
            InitializeInternal(call);
        }

        /// <summary>
        /// Starts a server side call. Currently, all server side calls are implemented as duplex 
        /// streaming call and they are adapted to the appropriate streaming arity.
        /// </summary>
        public Task ServerSideCallAsync(IObserver<TRequest> readObserver)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                this.readObserver = readObserver;

                call.StartServerSide(finishedServersideHandler);
                StartReceiveMessage();
                return finishedServersideTcs.Task;
            }
        }

        /// <summary>
        /// Sends a streaming response. Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendMessage(TResponse msg, AsyncCompletionDelegate completionDelegate)
        {
            StartSendMessageInternal(msg, completionDelegate);
        }

        /// <summary>
        /// Sends call result status, also indicating server is done with streaming responses.
        /// Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendStatusFromServer(Status status, AsyncCompletionDelegate completionDelegate)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");
                CheckSendingAllowed();

                call.StartSendStatusFromServer(status, halfclosedHandler);
                halfcloseRequested = true;
                sendCompletionDelegate = completionDelegate;
            }
        }

        /// <summary>
        /// Handles the server side close completion.
        /// </summary>
        private void HandleFinishedServerside(bool wasError, BatchContextSafeHandleNotOwned ctx)
        {
            lock (myLock)
            {
                finished = true;

                ReleaseResourcesIfPossible();
            }
            // TODO: handle error ...

            finishedServersideTcs.SetResult(null);
        }
    }
}