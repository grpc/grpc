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
    /// Manages client side native call lifecycle.
    /// </summary>
    internal class AsyncCall<TRequest, TResponse> : AsyncCallBase<TRequest, TResponse>
    {
        readonly CompletionCallbackDelegate unaryResponseHandler;
        readonly CompletionCallbackDelegate finishedHandler;

        // Completion of a pending unary response if not null.
        TaskCompletionSource<TResponse> unaryResponseTcs;

        // Set after status is received. Only used for streaming response calls.
        Status? finishedStatus;

        bool readObserverCompleted;  // True if readObserver has already been completed.

        public AsyncCall(Func<TRequest, byte[]> serializer, Func<byte[], TResponse> deserializer) : base(serializer, deserializer)
        {
            this.unaryResponseHandler = CreateBatchCompletionCallback(HandleUnaryResponse);
            this.finishedHandler = CreateBatchCompletionCallback(HandleFinished);
        }

        public void Initialize(Channel channel, CompletionQueueSafeHandle cq, string methodName)
        {
            var call = CallSafeHandle.Create(channel.Handle, cq, methodName, channel.Target, Timespec.InfFuture);
            DebugStats.ActiveClientCalls.Increment();
            InitializeInternal(call);
        }

        // TODO: this method is not Async, so it shouldn't be in AsyncCall class, but 
        // it is reusing fair amount of code in this class, so we are leaving it here.
        // TODO: for other calls, you need to call Initialize, this methods calls initialize 
        // on its own, so there's a usage inconsistency.
        /// <summary>
        /// Blocking unary request - unary response call.
        /// </summary>
        public TResponse UnaryCall(Channel channel, string methodName, TRequest msg, Metadata headers)
        {
            using (CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.Create())
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

                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.BlockingUnary(cq, payload, unaryResponseHandler, metadataArray);
                }

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
        public Task<TResponse> UnaryCallAsync(TRequest msg, Metadata headers)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                halfcloseRequested = true;
                readingDone = true;

                byte[] payload = UnsafeSerialize(msg);

                unaryResponseTcs = new TaskCompletionSource<TResponse>();
                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartUnary(payload, unaryResponseHandler, metadataArray);
                }
                return unaryResponseTcs.Task;
            }
        }

        /// <summary>
        /// Starts a streamed request - unary response call.
        /// Use StartSendMessage and StartSendCloseFromClient to stream requests.
        /// </summary>
        public Task<TResponse> ClientStreamingCallAsync(Metadata headers)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                readingDone = true;

                unaryResponseTcs = new TaskCompletionSource<TResponse>();
                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartClientStreaming(unaryResponseHandler, metadataArray);
                }

                return unaryResponseTcs.Task;
            }
        }

        /// <summary>
        /// Starts a unary request - streamed response call.
        /// </summary>
        public void StartServerStreamingCall(TRequest msg, Metadata headers)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;
                halfcloseRequested = true;
                halfclosed = true;  // halfclose not confirmed yet, but it will be once finishedHandler is called.

                byte[] payload = UnsafeSerialize(msg);

                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartServerStreaming(payload, finishedHandler, metadataArray);
                }
            }
        }

        /// <summary>
        /// Starts a streaming request - streaming response call.
        /// Use StartSendMessage and StartSendCloseFromClient to stream requests.
        /// </summary>
        public void StartDuplexStreamingCall(Metadata headers)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(call);

                started = true;

                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartDuplexStreaming(finishedHandler, metadataArray);
                }
            }
        }

        /// <summary>
        /// Sends a streaming request. Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendMessage(TRequest msg, AsyncCompletionDelegate<object> completionDelegate)
        {
            StartSendMessageInternal(msg, completionDelegate);
        }

        /// <summary>
        /// Receives a streaming response. Only one pending read action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartReadMessage(AsyncCompletionDelegate<TResponse> completionDelegate)
        {
            StartReadMessageInternal(completionDelegate);
        }

        /// <summary>
        /// Sends halfclose, indicating client is done with streaming requests.
        /// Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendCloseFromClient(AsyncCompletionDelegate<object> completionDelegate)
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
        /// On client-side, we only fire readCompletionDelegate once all messages have been read 
        /// and status has been received.
        /// </summary>
        protected override void ProcessLastRead(AsyncCompletionDelegate<TResponse> completionDelegate)
        {
            if (completionDelegate != null && readingDone && finishedStatus.HasValue)
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
                        FireCompletion(completionDelegate, default(TResponse), new RpcException(status));
                    }
                    else
                    {
                        FireCompletion(completionDelegate, default(TResponse), null);
                    }
                }
            }
        }

        protected override void OnReleaseResources()
        {
            DebugStats.ActiveClientCalls.Decrement();
        }

        /// <summary>
        /// Handler for unary response completion.
        /// </summary>
        private void HandleUnaryResponse(bool success, BatchContextSafeHandleNotOwned ctx)
        {
            lock (myLock)
            {
                finished = true;
                halfclosed = true;

                ReleaseResourcesIfPossible();
            }

            if (!success)
            {
                unaryResponseTcs.SetException(new RpcException(new Status(StatusCode.Internal, "Internal error occured.")));
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
        private void HandleFinished(bool success, BatchContextSafeHandleNotOwned ctx)
        {
            var status = ctx.GetReceivedStatus();

            AsyncCompletionDelegate<TResponse> origReadCompletionDelegate = null;
            lock (myLock)
            {
                finished = true;
                finishedStatus = status;

                origReadCompletionDelegate = readCompletionDelegate;

                ReleaseResourcesIfPossible();
            }

            ProcessLastRead(origReadCompletionDelegate);
        }
    }
}