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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Logging;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Manages client side native call lifecycle.
    /// </summary>
    internal class AsyncCall<TRequest, TResponse> : AsyncCallBase<TRequest, TResponse>, IUnaryResponseClientCallback, IReceivedStatusOnClientCallback, IReceivedResponseHeadersCallback
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<AsyncCall<TRequest, TResponse>>();

        readonly CallInvocationDetails<TRequest, TResponse> details;
        readonly INativeCall injectedNativeCall;  // for testing

        bool registeredWithChannel;

        // Dispose of to de-register cancellation token registration
        CancellationTokenRegistration cancellationTokenRegistration;

        // Completion of a pending unary response if not null.
        TaskCompletionSource<TResponse> unaryResponseTcs;

        // Completion of a streaming response call if not null.
        TaskCompletionSource<object> streamingResponseCallFinishedTcs;

        // TODO(jtattermusch): this field could be lazy-initialized (only if someone requests the response headers).
        // Response headers set here once received.
        TaskCompletionSource<Metadata> responseHeadersTcs = new TaskCompletionSource<Metadata>();

        // Set after status is received. Used for both unary and streaming response calls.
        ClientSideStatus? finishedStatus;

        public AsyncCall(CallInvocationDetails<TRequest, TResponse> callDetails)
            : base(callDetails.RequestMarshaller.ContextualSerializer, callDetails.ResponseMarshaller.ContextualDeserializer)
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
            using (CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.CreateSync())
            {
                bool callStartedOk = false;
                try
                {
                    unaryResponseTcs = new TaskCompletionSource<TResponse>();

                    lock (myLock)
                    {
                        GrpcPreconditions.CheckState(!started);
                        started = true;
                        Initialize(cq);

                        halfcloseRequested = true;
                        readingDone = true;
                    }

                    using (var serializationScope = DefaultSerializationContext.GetInitializedThreadLocalScope())
                    using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                    {
                        var payload = UnsafeSerialize(msg, serializationScope.Context); // do before metadata array?
                        var ctx = details.Channel.Environment.BatchContextPool.Lease();
                        try
                        {
                            call.StartUnary(ctx, payload, GetWriteFlagsForCall(), metadataArray, details.Options.Flags);
                            callStartedOk = true;

                            var ev = cq.Pluck(ctx.Handle);
                            bool success = (ev.success != 0);
                            try
                            {
                                using (profiler.NewScope("AsyncCall.UnaryCall.HandleBatch"))
                                {
                                    HandleUnaryResponse(success, ctx.GetReceivedStatusOnClient(), ctx.GetReceivedMessageReader(), ctx.GetReceivedInitialMetadata());
                                }
                            }
                            catch (Exception e)
                            {
                                Logger.Error(e, "Exception occurred while invoking completion delegate.");
                            }
                        }
                        finally
                        {
                            ctx.Recycle();
                        }
                    }
                }
                finally
                {
                    if (!callStartedOk)
                    {
                        lock (myLock)
                        {
                            OnFailedToStartCallLocked();
                        }
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
                bool callStartedOk = false;
                try
                {
                    GrpcPreconditions.CheckState(!started);
                    started = true;

                    Initialize(details.Channel.CompletionQueue);

                    halfcloseRequested = true;
                    readingDone = true;

                    using (var serializationScope = DefaultSerializationContext.GetInitializedThreadLocalScope())
                    {
                        var payload = UnsafeSerialize(msg, serializationScope.Context);
                        unaryResponseTcs = new TaskCompletionSource<TResponse>();
                        using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                        {
                            call.StartUnary(UnaryResponseClientCallback, payload, GetWriteFlagsForCall(), metadataArray, details.Options.Flags);
                            callStartedOk = true;
                        }
                    }

                    return unaryResponseTcs.Task;
                }
                finally
                {
                    if (!callStartedOk)
                    {
                        OnFailedToStartCallLocked();
                    }
                }
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
                bool callStartedOk = false;
                try
                {
                    GrpcPreconditions.CheckState(!started);
                    started = true;

                    Initialize(details.Channel.CompletionQueue);

                    readingDone = true;

                    unaryResponseTcs = new TaskCompletionSource<TResponse>();
                    using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                    {
                        call.StartClientStreaming(UnaryResponseClientCallback, metadataArray, details.Options.Flags);
                        callStartedOk = true;
                    }

                    return unaryResponseTcs.Task;
                }
                finally
                {
                    if (!callStartedOk)
                    {
                        OnFailedToStartCallLocked();
                    }
                }
            }
        }

        /// <summary>
        /// Starts a unary request - streamed response call.
        /// </summary>
        public void StartServerStreamingCall(TRequest msg)
        {
            lock (myLock)
            {
                bool callStartedOk = false;
                try
                {
                    GrpcPreconditions.CheckState(!started);
                    started = true;

                    Initialize(details.Channel.CompletionQueue);

                    halfcloseRequested = true;

                    using (var serializationScope = DefaultSerializationContext.GetInitializedThreadLocalScope())
                    {
                        var payload = UnsafeSerialize(msg, serializationScope.Context);
                        streamingResponseCallFinishedTcs = new TaskCompletionSource<object>();
                        using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                        {
                            call.StartServerStreaming(ReceivedStatusOnClientCallback, payload, GetWriteFlagsForCall(), metadataArray, details.Options.Flags);
                            callStartedOk = true;
                        }
                    }
                    call.StartReceiveInitialMetadata(ReceivedResponseHeadersCallback);
                }
                finally
                {
                    if (!callStartedOk)
                    {
                        OnFailedToStartCallLocked();
                    }
                }
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
                bool callStartedOk = false;
                try
                {
                    GrpcPreconditions.CheckState(!started);
                    started = true;

                    Initialize(details.Channel.CompletionQueue);

                    streamingResponseCallFinishedTcs = new TaskCompletionSource<object>();
                    using (var metadataArray = MetadataArraySafeHandle.Create(details.Options.Headers))
                    {
                        call.StartDuplexStreaming(ReceivedStatusOnClientCallback, metadataArray, details.Options.Flags);
                        callStartedOk = true;
                    }
                    call.StartReceiveInitialMetadata(ReceivedResponseHeadersCallback);
                }
                finally
                {
                    if (!callStartedOk)
                    {
                        OnFailedToStartCallLocked();
                    }
                }
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
                    return TaskUtils.CompletedTask;
                }
                call.StartSendCloseFromClient(SendCompletionCallback);

                halfcloseRequested = true;
                streamingWriteTcs = new TaskCompletionSource<object>();
                return streamingWriteTcs.Task;
            }
        }

        /// <summary>
        /// Get the task that completes once if streaming response call finishes with ok status and throws RpcException with given status otherwise.
        /// </summary>
        public Task StreamingResponseCallFinishedTask
        {
            get
            {
                return streamingResponseCallFinishedTcs.Task;
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

        protected override void OnAfterReleaseResourcesLocked()
        {
            if (registeredWithChannel)
            {
                details.Channel.RemoveCallReference(this);
                registeredWithChannel = false;
            }
        }

        protected override void OnAfterReleaseResourcesUnlocked()
        {
            // If cancellation callback is in progress, this can block
            // so we need to do this outside of call's lock to prevent
            // deadlock.
            // See https://github.com/grpc/grpc/issues/14777
            // See https://github.com/dotnet/corefx/issues/14903
            cancellationTokenRegistration.Dispose();
        }

        protected override bool IsClient
        {
            get { return true; }
        }

        protected override Exception GetRpcExceptionClientOnly()
        {
            return new RpcException(finishedStatus.Value.Status, finishedStatus.Value.Trailers);
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
                tcs.SetException(new RpcException(finishedStatus.Value.Status, finishedStatus.Value.Trailers));
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
            var call = CreateNativeCall(cq);

            details.Channel.AddCallReference(this);
            registeredWithChannel = true;
            InitializeInternal(call);

            RegisterCancellationCallback();
        }

        private void OnFailedToStartCallLocked()
        {
            ReleaseResources();

            // We need to execute the hook that disposes the cancellation token
            // registration, but it cannot be done from under a lock.
            // To make things simple, we just schedule the unregistering
            // on a threadpool.
            // - Once the native call is disposed, the Cancel() calls are ignored anyway
            // - We don't care about the overhead as OnFailedToStartCallLocked() only happens
            //   when something goes very bad when initializing a call and that should
            //   never happen when gRPC is used correctly.
            ThreadPool.QueueUserWorkItem((state) => OnAfterReleaseResourcesUnlocked());
        }

        private INativeCall CreateNativeCall(CompletionQueueSafeHandle cq)
        {
            if (injectedNativeCall != null)
            {
                return injectedNativeCall;  // allows injecting a mock INativeCall in tests.
            }

            var parentCall = details.Options.PropagationToken.AsImplOrNull()?.ParentCall ?? CallSafeHandle.NullInstance;

            var credentials = details.Options.Credentials;
            using (var nativeCredentials = credentials != null ? credentials.ToNativeCredentials() : null)
            {
                var result = details.Channel.Handle.CreateCall(
                             parentCall, ContextPropagationTokenImpl.DefaultMask, cq,
                             details.Method, details.Host, Timespec.FromDateTime(details.Options.Deadline.Value), nativeCredentials);
                return result;
            }
        }

        // Make sure that once cancellationToken for this call is cancelled, Cancel() will be called.
        private void RegisterCancellationCallback()
        {
            cancellationTokenRegistration = RegisterCancellationCallbackForToken(details.Options.CancellationToken);
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
        private void HandleUnaryResponse(bool success, ClientSideStatus receivedStatus, IBufferReader receivedMessageReader, Metadata responseHeaders)
        {
            // NOTE: because this event is a result of batch containing GRPC_OP_RECV_STATUS_ON_CLIENT,
            // success will be always set to true.

            TaskCompletionSource<object> delayedStreamingWriteTcs = null;
            TResponse msg = default(TResponse);
            var deserializeException = TryDeserialize(receivedMessageReader, out msg);

            bool releasedResources;
            lock (myLock)
            {
                finished = true;

                if (deserializeException != null && receivedStatus.Status.StatusCode == StatusCode.OK)
                {
                    receivedStatus = new ClientSideStatus(DeserializeResponseFailureStatus, receivedStatus.Trailers);
                }
                finishedStatus = receivedStatus;

                if (isStreamingWriteCompletionDelayed)
                {
                    delayedStreamingWriteTcs = streamingWriteTcs;
                    streamingWriteTcs = null;
                }

                releasedResources = ReleaseResourcesIfPossible();
            }

            if (releasedResources)
            {
                OnAfterReleaseResourcesUnlocked();
            }

            responseHeadersTcs.SetResult(responseHeaders);

            if (delayedStreamingWriteTcs != null)
            {
                delayedStreamingWriteTcs.SetException(GetRpcExceptionClientOnly());
            }

            var status = receivedStatus.Status;
            if (status.StatusCode != StatusCode.OK)
            {
                unaryResponseTcs.SetException(new RpcException(status, receivedStatus.Trailers));
                return;
            }

            unaryResponseTcs.SetResult(msg);
        }

        /// <summary>
        /// Handles receive status completion for calls with streaming response.
        /// </summary>
        private void HandleFinished(bool success, ClientSideStatus receivedStatus)
        {
            // NOTE: because this event is a result of batch containing GRPC_OP_RECV_STATUS_ON_CLIENT,
            // success will be always set to true.

            TaskCompletionSource<object> delayedStreamingWriteTcs = null;

            bool releasedResources;
            bool origCancelRequested;
            lock (myLock)
            {
                finished = true;
                finishedStatus = receivedStatus;
                if (isStreamingWriteCompletionDelayed)
                {
                    delayedStreamingWriteTcs = streamingWriteTcs;
                    streamingWriteTcs = null;
                }

                releasedResources = ReleaseResourcesIfPossible();
                origCancelRequested = cancelRequested;
            }

            if (releasedResources)
            {
                OnAfterReleaseResourcesUnlocked();
            }

            if (delayedStreamingWriteTcs != null)
            {
                delayedStreamingWriteTcs.SetException(GetRpcExceptionClientOnly());
            }

            var status = receivedStatus.Status;
            if (status.StatusCode != StatusCode.OK)
            {
                streamingResponseCallFinishedTcs.SetException(new RpcException(status, receivedStatus.Trailers));
                if (status.StatusCode == StatusCode.Cancelled || origCancelRequested)
                {
                    // Make sure the exception set to the Task is observed,
                    // otherwise this can trigger "Unobserved exception" when the response stream
                    // is not read until its end and the task created by the TCS is garbage collected.
                    // See https://github.com/grpc/grpc/issues/17458
                    var _ = streamingResponseCallFinishedTcs.Task.Exception;
                }
                return;
            }

            streamingResponseCallFinishedTcs.SetResult(null);
        }

        IUnaryResponseClientCallback UnaryResponseClientCallback => this;

        void IUnaryResponseClientCallback.OnUnaryResponseClient(bool success, ClientSideStatus receivedStatus, IBufferReader receivedMessageReader, Metadata responseHeaders)
        {
            HandleUnaryResponse(success, receivedStatus, receivedMessageReader, responseHeaders);
        }

        IReceivedStatusOnClientCallback ReceivedStatusOnClientCallback => this;

        void IReceivedStatusOnClientCallback.OnReceivedStatusOnClient(bool success, ClientSideStatus receivedStatus)
        {
            HandleFinished(success, receivedStatus);
        }

        IReceivedResponseHeadersCallback ReceivedResponseHeadersCallback => this;

        void IReceivedResponseHeadersCallback.OnReceivedResponseHeaders(bool success, Metadata responseHeaders)
        {
            HandleReceivedResponseHeaders(success, responseHeaders);
        }
    }
}
