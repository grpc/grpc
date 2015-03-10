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
    /// Handles client side native call lifecycle.
    /// </summary>
    internal class AsyncCall<TRequest, TResponse> : AsyncCallBase<TRequest, TResponse>
    {
        readonly CompletionCallbackDelegate unaryResponseHandler;
        readonly CompletionCallbackDelegate finishedHandler;

        // Completion of a pending unary response if not null.
        TaskCompletionSource<TResponse> unaryResponseTcs;

        // Set after status is received. Only used for streaming response calls.
        Nullable<Status> finishedStatus;

        bool readObserverCompleted;  // True if readObserver has already been completed.

        public AsyncCall(Func<TRequest, byte[]> serializer, Func<byte[], TResponse> deserializer) : base(serializer, deserializer)
        {
            this.unaryResponseHandler = CreateBatchCompletionCallback(HandleUnaryResponse);
            this.finishedHandler = CreateBatchCompletionCallback(HandleFinished);
        }

        public void Initialize(Channel channel, CompletionQueueSafeHandle cq, String methodName)
        {
            var call = CallSafeHandle.Create(channel.Handle, cq, methodName, channel.Target, Timespec.InfFuture);
            InitializeInternal(call);
        }

        // TODO: this method is not Async, so it shouldn't be in AsyncCall class, but 
        // it is reusing fair amount of code in this class, so we are leaving it here.
        // TODO: for other calls, you need to call Initialize, this methods calls initialize 
        // on its own, so there's a usage inconsistency.
        /// <summary>
        /// Blocking unary request - unary response call.
        /// </summary>
        public TResponse UnaryCall(Channel channel, String methodName, TRequest msg)
        {
            using(CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.Create())
            {
                byte[] payload = UnsafeSerialize(msg);

                unaryResponseTcs = new TaskCompletionSource<TResponse>();

                lock (myLock)
                {
                    Initialize(channel, cq, methodName);
                    started = true;
                    halfcloseRequested = true;
                    readingDone = true;
                }
                call.BlockingUnary(cq, payload, unaryResponseHandler);

                try
                {
                    // Once the blocking call returns, the result should be available synchronously.
                    return unaryResponseTcs.Task.Result;
                }
                catch (AggregateException ae)
                {
                    throw ExceptionHelper.UnwrapRpcException(ae);
                }
            }
        }

        /// <summary>
        /// Starts a unary request - unary response call.
        /// </summary>
        public Task<TResponse> UnaryCallAsync(TRequest msg)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                halfcloseRequested = true;
                readingDone = true;

                byte[] payload = UnsafeSerialize(msg);

                unaryResponseTcs = new TaskCompletionSource<TResponse>();
                call.StartUnary(payload, unaryResponseHandler);

                return unaryResponseTcs.Task;
            }
        }

        /// <summary>
        /// Starts a streamed request - unary response call.
        /// Use StartSendMessage and StartSendCloseFromClient to stream requests.
        /// </summary>
        public Task<TResponse> ClientStreamingCallAsync()
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                readingDone = true;

                unaryResponseTcs = new TaskCompletionSource<TResponse>();
                call.StartClientStreaming(unaryResponseHandler);

                return unaryResponseTcs.Task;
            }
        }

        /// <summary>
        /// Starts a unary request - streamed response call.
        /// </summary>
        public void StartServerStreamingCall(TRequest msg, IObserver<TResponse> readObserver)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                halfcloseRequested = true;
                halfclosed = true;  // halfclose not confirmed yet, but it will be once finishedHandler is called.
        
                this.readObserver = readObserver;

                byte[] payload = UnsafeSerialize(msg);
        
                call.StartServerStreaming(payload, finishedHandler);

                StartReceiveMessage();
            }
        }

        /// <summary>
        /// Starts a streaming request - streaming response call.
        /// Use StartSendMessage and StartSendCloseFromClient to stream requests.
        /// </summary>
        public void StartDuplexStreamingCall(IObserver<TResponse> readObserver)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;

                this.readObserver = readObserver;

                call.StartDuplexStreaming(finishedHandler);

                StartReceiveMessage();
            }
        }

        /// <summary>
        /// Sends a streaming request. Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendMessage(TRequest msg, AsyncCompletionDelegate completionDelegate)
        {
            StartSendMessageInternal(msg, completionDelegate);
        }

        /// <summary>
        /// Sends halfclose, indicating client is done with streaming requests.
        /// Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendCloseFromClient(AsyncCompletionDelegate completionDelegate)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");
                CheckSendingAllowed();

                call.StartSendCloseFromClient(halfclosedHandler);

                halfcloseRequested = true;
                sendCompletionDelegate = completionDelegate;
            }
        }

        /// <summary>
        /// On client-side, we only fire readObserver.OnCompleted once all messages have been read 
        /// and status has been received.
        /// </summary>
        protected override void CompleteReadObserver()
        {
            if (readingDone && finishedStatus.HasValue)
            {
                bool shouldComplete;
                lock (myLock)
                {
                    shouldComplete = !readObserverCompleted;
                    readObserverCompleted = true;
                }

                if (shouldComplete)
                {
                    var status = finishedStatus.Value;
                    if (status.StatusCode != StatusCode.OK)
                    {
                        FireReadObserverOnError(new RpcException(status));
                    }
                    else
                    {
                        FireReadObserverOnCompleted();
                    }
                }
            }
        }

        /// <summary>
        /// Handler for unary response completion.
        /// </summary>
        private void HandleUnaryResponse(bool wasError, BatchContextSafeHandleNotOwned ctx)
        {
            lock(myLock)
            {
                finished = true;
                halfclosed = true;

                ReleaseResourcesIfPossible();
            }

            if (wasError)
            {
                unaryResponseTcs.SetException(new RpcException(
                    new Status(StatusCode.Internal, "Internal error occured.")
                ));
                return;
            }

            var status = ctx.GetReceivedStatus();
            if (status.StatusCode != StatusCode.OK)
            {
                unaryResponseTcs.SetException(new RpcException(status));
                return;
            }

            // TODO: handle deserialization error
            TResponse msg;
            TryDeserialize(ctx.GetReceivedMessage(), out msg);

            unaryResponseTcs.SetResult(msg);
        }

        /// <summary>
        /// Handles receive status completion for calls with streaming response.
        /// </summary>
        private void HandleFinished(bool wasError, BatchContextSafeHandleNotOwned ctx)
        {
            var status = ctx.GetReceivedStatus();

            lock (myLock)
            {
                finished = true;
                finishedStatus = status;

                ReleaseResourcesIfPossible();
            }

            CompleteReadObserver();
        }
    }
}