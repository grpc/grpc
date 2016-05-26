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
using Grpc.Core.Logging;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Manages client side native call lifecycle.
    /// </summary>
    internal class AsyncCall<TRequest, TResponse> : AsyncCallBase<TRequest, TResponse>
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<AsyncCall<TRequest, TResponse>>();

        readonly CallInvocationDetails<TRequest, TResponse> details;
        readonly INativeCall injectedNativeCall;  // for testing

        // Completion of a pending unary response if not null.
        TaskCompletionSource<TResponse> unaryResponseTcs;

        // TODO(jtattermusch): this field doesn't need to be initialized for unary response calls.
        // Indicates that response streaming call has finished.
        TaskCompletionSource<object> streamingCallFinishedTcs = new TaskCompletionSource<object>();

        // TODO(jtattermusch): this field could be lazy-initialized (only if someone requests the response headers).
        // Response headers set here once received.
        TaskCompletionSource<Metadata> responseHeadersTcs = new TaskCompletionSource<Metadata>();

        // Set after status is received. Used for both unary and streaming response calls.
        ClientSideStatus? finishedStatus;

        public AsyncCall(CallInvocationDetails<TRequest, TResponse> callDetails)
            : base(callDetails.RequestMarshaller.Serializer, callDetails.ResponseMarshaller.Deserializer)
        {
            this.details = callDetails.WithOptions(callDetails.Options.Normalize());
            this.initialMetadataSent = true;  // we always send metadata at the very beginning of the call.
        }

        /// <summary>
        /// This constructor should only be used for testing.
        /// </summary>
        public AsyncCall(CallInvocationDetails<TRequest, TResponse> callDetails, INativeCall injectedNativeCall) : this(callDetails)
        {
            this.injectedNativeCall = injectedNativeCall;
        }

        // TODO: this method is not Async, so it shouldn't be in AsyncCall class, but 
        // it is reusing fair amount of code in this class, so we are leaving it here.
        /// <summary>
        /// Blocking unary request - unary response call.
        /// </summary>
        public TResponse UnaryCall(TRequest msg)
        {
            var profiler = Profilers.ForCurrentThread();

            using (profiler.NewScope("AsyncCall.UnaryCall"))
            using (CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.Create())
            {
                byte[] payload = UnsafeSerialize(msg);

                unaryResponseTcs = new TaskCompletionSource<TResponse>();

                lock (myLock)
                {
                    GrpcPreconditions.CheckState(!started);
                    started = true;
                    Initialize(cq);

                    halfcloseRequested = true;
                    readingDone = true;
                }

                using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                using (var ctx = BatchContextSafeHandle.Create())
                {
                    call.StartUnary(ctx, payload, metadataArray, GetWriteFlagsForCall());

                    var ev = cq.Pluck(ctx.Handle);

                    bool success = (ev.success != 0);
                    try
                    {
                        using (profiler.NewScope("AsyncCall.UnaryCall.HandleBatch"))
                        {
                            HandleUnaryResponse(success, ctx.GetReceivedStatusOnClient(), ctx.GetReceivedMessage(), ctx.GetReceivedInitialMetadata());
                        }
                    }
                    catch (Exception e)
                    {
                        Logger.Error(e, "Exception occured while invoking completion delegate.");
                    }
                }
                    
                // Once the blocking call returns, the result should be available synchronously.
                // Note that GetAwaiter().GetResult() doesn't wrap exceptions in AggregateException.
                return unaryResponseTcs.Task.GetAwaiter().GetResult();
            }
        }

        /// <summary>
        /// Starts a unary request - unary response call.
        /// </summary>
        public Task<TResponse> UnaryCallAsync(TRequest msg)
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!started);
                started = true;

                Initialize(details.Channel.CompletionQueue);

                halfcloseRequested = true;
                readingDone = true;

                byte[] payload = UnsafeSerialize(msg);

                unaryResponseTcs = new TaskCompletionSource<TResponse>();
                using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                {
                    call.StartUnary(HandleUnaryResponse, payload, metadataArray, GetWriteFlagsForCall());
                }
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
                GrpcPreconditions.CheckState(!started);
                started = true;

                Initialize(details.Channel.CompletionQueue);

                readingDone = true;

                unaryResponseTcs = new TaskCompletionSource<TResponse>();
                using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                {
                    call.StartClientStreaming(HandleUnaryResponse, metadataArray);
                }

                return unaryResponseTcs.Task;
            }
        }

        /// <summary>
        /// Starts a unary request - streamed response call.
        /// </summary>
        public void StartServerStreamingCall(TRequest msg)
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!started);
                started = true;

                Initialize(details.Channel.CompletionQueue);

                halfcloseRequested = true;

                byte[] payload = UnsafeSerialize(msg);

                using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                {
                    call.StartServerStreaming(HandleFinished, payload, metadataArray, GetWriteFlagsForCall());
                }
                call.StartReceiveInitialMetadata(HandleReceivedResponseHeaders);
            }
        }

        /// <summary>
        /// Starts a streaming request - streaming response call.
        /// Use StartSendMessage and StartSendCloseFromClient to stream requests.
        /// </summary>
        public void StartDuplexStreamingCall()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!started);
                started = true;

                Initialize(details.Channel.CompletionQueue);

                using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                {
                    call.StartDuplexStreaming(HandleFinished, metadataArray);
                }
                call.StartReceiveInitialMetadata(HandleReceivedResponseHeaders);
            }
        }

        /// <summary>
        /// Sends a streaming request. Only one pending send action is allowed at any given time.
        /// </summary>
        public Task SendMessageAsync(TRequest msg, WriteFlags writeFlags)
        {
            return SendMessageInternalAsync(msg, writeFlags);
        }

        /// <summary>
        /// Receives a streaming response. Only one pending read action is allowed at any given time.
        /// </summary>
        public Task<TResponse> ReadMessageAsync()
        {
            return ReadMessageInternalAsync();
        }

        /// <summary>
        /// Sends halfclose, indicating client is done with streaming requests.
        /// Only one pending send action is allowed at any given time.
        /// </summary>
        public Task SendCloseFromClientAsync()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(started);

                var earlyResult = CheckSendPreconditionsClientSide();
                if (earlyResult != null)
                {
                    return earlyResult;
                }

                if (disposed || finished)
                {
                    // In case the call has already been finished by the serverside,
                    // the halfclose has already been done implicitly, so just return
                    // completed task here.
                    halfcloseRequested = true;
                    return Task.FromResult<object>(null);
                }
                call.StartSendCloseFromClient(HandleSendCloseFromClientFinished);

                halfcloseRequested = true;
                streamingWriteTcs = new TaskCompletionSource<object>();
                return streamingWriteTcs.Task;
            }
        }

        /// <summary>
        /// Get the task that completes once if streaming call finishes with ok status and throws RpcException with given status otherwise.
        /// </summary>
        public Task StreamingCallFinishedTask
        {
            get
            {
                return streamingCallFinishedTcs.Task;
            }
        }

        /// <summary>
        /// Get the task that completes once response headers are received.
        /// </summary>
        public Task<Metadata> ResponseHeadersAsync
        {
            get
            {
                return responseHeadersTcs.Task;
            }
        }

        /// <summary>
        /// Gets the resulting status if the call has already finished.
        /// Throws InvalidOperationException otherwise.
        /// </summary>
        public Status GetStatus()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(finishedStatus.HasValue, "Status can only be accessed once the call has finished.");
                return finishedStatus.Value.Status;
            }
        }

        /// <summary>
        /// Gets the trailing metadata if the call has already finished.
        /// Throws InvalidOperationException otherwise.
        /// </summary>
        public Metadata GetTrailers()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(finishedStatus.HasValue, "Trailers can only be accessed once the call has finished.");
                return finishedStatus.Value.Trailers;
            }
        }

        public CallInvocationDetails<TRequest, TResponse> Details
        {
            get
            {
                return this.details;
            }
        }

        protected override void OnAfterReleaseResources()
        {
            details.Channel.RemoveCallReference(this);
        }

        protected override bool IsClient
        {
            get { return true; }
        }

        protected override Task CheckSendAllowedOrEarlyResult()
        {
            var earlyResult = CheckSendPreconditionsClientSide();
            if (earlyResult != null)
            {
                return earlyResult;
            }

            if (finishedStatus.HasValue)
            {
                // throwing RpcException if we already received status on client
                // side makes the most sense.
                // Note that this throws even for StatusCode.OK.
                // Writing after the call has finished is not a programming error because server can close
                // the call anytime, so don't throw directly, but let the write task finish with an error.
                var tcs = new TaskCompletionSource<object>();
                tcs.SetException(new RpcException(finishedStatus.Value.Status));
                return tcs.Task;
            }

            return null;
        }

        private Task CheckSendPreconditionsClientSide()
        {
            GrpcPreconditions.CheckState(!halfcloseRequested, "Request stream has already been completed.");
            GrpcPreconditions.CheckState(streamingWriteTcs == null, "Only one write can be pending at a time.");

            if (cancelRequested)
            {
                // Return a cancelled task.
                var tcs = new TaskCompletionSource<object>();
                tcs.SetCanceled();
                return tcs.Task;
            }

            return null;
        }

        private void Initialize(CompletionQueueSafeHandle cq)
        {
            using (Profilers.ForCurrentThread().NewScope("AsyncCall.Initialize"))
            { 
                var call = CreateNativeCall(cq);

                details.Channel.AddCallReference(this);
                InitializeInternal(call);
                RegisterCancellationCallback();
            }
        }

        private INativeCall CreateNativeCall(CompletionQueueSafeHandle cq)
        {
            using (Profilers.ForCurrentThread().NewScope("AsyncCall.CreateNativeCall"))
            { 
                if (injectedNativeCall != null)
                {
                    return injectedNativeCall;  // allows injecting a mock INativeCall in tests.
                }

                var parentCall = details.Options.PropagationToken != null ? details.Options.PropagationToken.ParentCall : CallSafeHandle.NullInstance;

                var credentials = details.Options.Credentials;
                using (var nativeCredentials = credentials != null ? credentials.ToNativeCredentials() : null)
                {
                    var result = details.Channel.Handle.CreateCall(
                                 parentCall, ContextPropagationToken.DefaultMask, cq,
                                 details.Method, details.Host, Timespec.FromDateTime(details.Options.Deadline.Value), nativeCredentials);
                    return result;
                }
            }
        }

        // Make sure that once cancellationToken for this call is cancelled, Cancel() will be called.
        private void RegisterCancellationCallback()
        {
            var token = details.Options.CancellationToken;
            if (token.CanBeCanceled)
            {
                token.Register(() => this.Cancel());
            }
        }

        /// <summary>
        /// Gets WriteFlags set in callDetails.Options.WriteOptions
        /// </summary>
        private WriteFlags GetWriteFlagsForCall()
        {
            var writeOptions = details.Options.WriteOptions;
            return writeOptions != null ? writeOptions.Flags : default(WriteFlags);
        }

        /// <summary>
        /// Handles receive status completion for calls with streaming response.
        /// </summary>
        private void HandleReceivedResponseHeaders(bool success, Metadata responseHeaders)
        {
            // TODO(jtattermusch): handle success==false
            responseHeadersTcs.SetResult(responseHeaders);
        }

        /// <summary>
        /// Handler for unary response completion.
        /// </summary>
        private void HandleUnaryResponse(bool success, ClientSideStatus receivedStatus, byte[] receivedMessage, Metadata responseHeaders)
        {
            // NOTE: because this event is a result of batch containing GRPC_OP_RECV_STATUS_ON_CLIENT,
            // success will be always set to true.

            using (Profilers.ForCurrentThread().NewScope("AsyncCall.HandleUnaryResponse"))
            {
                TResponse msg = default(TResponse);
                var deserializeException = TryDeserialize(receivedMessage, out msg);

                lock (myLock)
                {
                    finished = true;

                    if (deserializeException != null && receivedStatus.Status.StatusCode == StatusCode.OK)
                    {
                        receivedStatus = new ClientSideStatus(DeserializeResponseFailureStatus, receivedStatus.Trailers);
                    }
                    finishedStatus = receivedStatus;

                    ReleaseResourcesIfPossible();
                }

                responseHeadersTcs.SetResult(responseHeaders);

                var status = receivedStatus.Status;

                if (status.StatusCode != StatusCode.OK)
                {
                    unaryResponseTcs.SetException(new RpcException(status));
                    return;
                }

                unaryResponseTcs.SetResult(msg);
            }
        }

        /// <summary>
        /// Handles receive status completion for calls with streaming response.
        /// </summary>
        private void HandleFinished(bool success, ClientSideStatus receivedStatus)
        {
            // NOTE: because this event is a result of batch containing GRPC_OP_RECV_STATUS_ON_CLIENT,
            // success will be always set to true.

            lock (myLock)
            {
                finished = true;
                finishedStatus = receivedStatus;

                ReleaseResourcesIfPossible();
            }

            var status = receivedStatus.Status;

            if (status.StatusCode != StatusCode.OK)
            {
                streamingCallFinishedTcs.SetException(new RpcException(status));
                return;
            }

            streamingCallFinishedTcs.SetResult(null);
        }
    }
}
