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
using System.IO;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Base for handling both client side and server side calls.
    /// Manages native call lifecycle and provides convenience methods.
    /// </summary>
    internal abstract class AsyncCallBase<TWrite, TRead> : IReceivedMessageCallback, ISendCompletionCallback
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<AsyncCallBase<TWrite, TRead>>();
        protected static readonly Status DeserializeResponseFailureStatus = new Status(StatusCode.Internal, "Failed to deserialize response message.");

        readonly Action<TWrite, SerializationContext> serializer;
        readonly Func<DeserializationContext, TRead> deserializer;

        protected readonly object myLock = new object();

        protected INativeCall call;

        private AsyncTaskMethodBuilder<TRead> streamingReadTmb; // Completion of a pending streaming read if not null.
        private AsyncTaskMethodBuilder streamingWriteTmb; // Completion of a pending streaming write or send close from client if not null.
        private AsyncTaskMethodBuilder sendStatusFromServerTmb;

        protected long streamingWritesCounter;  // Number of streaming send operations started so far.

#region State flags and accessors/helpers
        [Flags]
        private enum StateFlags // flags rather than discrete bools
        {
            None = 0,
            Disposed = 1 << 0,
            Started = 1 << 1,
            CancelRequested = 1 << 2,
            IsStreamingWriteCompletionDelayed = 1 << 3, // Only used for the client side.
            ReadingDone = 1 << 4, // True if last read (i.e. read with null payload) was already received.
            HalfCloseRequested = 1 << 5, // True if send close have been initiated.
            Finished = 1 << 6, // True if close has been received from the peer.
            InitialMetadataSent = 1 << 7,
            ReceiveResponseHeadersPending = 1 << 8, // True if this is a call with streaming response and the recv_initial_metadata_on_client operation hasn't finished yet.
            StreamingReadInitialized = 1 << 9, // whether the corresponding TMB is considered initialized
            StreamingWriteInitialized = 1 << 10, // whether the corresponding TMB is considered initialized
            SendStatusFromServerInitialized = 1 << 12, // whether the corresponding TMB is considered initialized
        }

        private int stateFlags; // int, not StateFlags, because of Volatile/Interlocked APIs

        // check whether a given flag is specified ("any")
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private bool GetState(StateFlags flag) => (Volatile.Read(ref stateFlags) & (int)flag) != 0;

        private bool SetState(StateFlags flag, bool value)
        {
            int oldValue, newValue;
            do
            {
                oldValue = Volatile.Read(ref stateFlags);
                newValue = value ? (oldValue | (int)flag) : (oldValue & ~(int)flag);
            }
            while (oldValue != newValue && Interlocked.CompareExchange(ref stateFlags, newValue, oldValue) != oldValue);
            return (oldValue & (int)flag) != 0; // return previous flag state ("any")
        }

        protected bool Disposed
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.Disposed);
            private set => SetState(StateFlags.Disposed, value);
        }
        protected bool Started
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.Started);
            set => SetState(StateFlags.Started, value);
        }
        protected bool CancelRequested
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.CancelRequested);
            private set => SetState(StateFlags.CancelRequested, value);
        }

        protected bool ReadingDone
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.ReadingDone);
            set => SetState(StateFlags.ReadingDone, value);
        }
        protected bool HalfCloseRequested
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.HalfCloseRequested);
            set => SetState(StateFlags.HalfCloseRequested, value);
        }

        protected bool Finished
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.Finished);
            set => SetState(StateFlags.Finished, value);
        }
        protected bool InitialMetadataSent
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.InitialMetadataSent);
            set => SetState(StateFlags.InitialMetadataSent, value);
        }
        protected bool SendStatusFromServerInitialized => GetState(StateFlags.SendStatusFromServerInitialized);
        protected bool ReceiveResponseHeadersPending
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.ReceiveResponseHeadersPending);
            set => SetState(StateFlags.ReceiveResponseHeadersPending, value);
        }
        protected bool IsStreamingWriteCompletionDelayed
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.IsStreamingWriteCompletionDelayed);
            private set => SetState(StateFlags.IsStreamingWriteCompletionDelayed, value);
        }

        protected bool StreamingWriteInitialized
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.StreamingWriteInitialized);
        }
        protected bool StreamingReadInitialized
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get => GetState(StateFlags.StreamingReadInitialized);
        }

        protected Task InitializeStreamingWrite()
        {
            streamingWriteTmb = AsyncTaskMethodBuilder.Create();
            SetState(StateFlags.StreamingWriteInitialized, true);
            return streamingWriteTmb.Task;
        }

        protected Task InitializeSendStatusFromServer()
        {
            sendStatusFromServerTmb = AsyncTaskMethodBuilder.Create();
            SetState(StateFlags.SendStatusFromServerInitialized, true);
            return sendStatusFromServerTmb.Task;
        }

        protected void InitializeStreamingRead(TRead value)
        {
            streamingReadTmb = AsyncTaskMethodBuilder<TRead>.Create();
            SetState(StateFlags.StreamingReadInitialized, true);
            streamingReadTmb.SetResult(value);
        }

        protected AsyncTaskMethodBuilder? ResetStreamingWrite()
        {
            if (SetState(StateFlags.StreamingWriteInitialized, false))
            {   // was initialized
                var tmp = streamingWriteTmb;
                streamingWriteTmb = default;
                return tmp;
            }
            else
            {   // wasn't initialized
                return null;
            }
        }
#endregion

        public AsyncCallBase(Action<TWrite, SerializationContext> serializer, Func<DeserializationContext, TRead> deserializer)
        {
            this.serializer = GrpcPreconditions.CheckNotNull(serializer);
            this.deserializer = GrpcPreconditions.CheckNotNull(deserializer);
        }

        /// <summary>
        /// Requests cancelling the call.
        /// </summary>
        public void Cancel()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(Started);
                CancelRequested = true;

                if (!Disposed)
                {
                    call.Cancel();
                }
            }
        }

        /// <summary>
        /// Requests cancelling the call with given status.
        /// </summary>
        protected void CancelWithStatus(Status status)
        {
            lock (myLock)
            {
                CancelRequested = true;

                if (!Disposed)
                {
                    call.CancelWithStatus(status);
                }
            }
        }

        protected void InitializeInternal(INativeCall call)
        {
            lock (myLock)
            {
                this.call = call;
            }
        }

        /// <summary>
        /// Initiates sending a message. Only one send operation can be active at a time.
        /// </summary>
        protected Task SendMessageInternalAsync(TWrite msg, WriteFlags writeFlags)
        {
            using (var serializationScope = DefaultSerializationContext.GetInitializedThreadLocalScope())
            {
                var payload = UnsafeSerialize(msg, serializationScope.Context);
                lock (myLock)
                {
                    GrpcPreconditions.CheckState(Started);
                    var earlyResult = CheckSendAllowedOrEarlyResult();
                    if (earlyResult != null)
                    {
                        return earlyResult;
                    }

                    call.StartSendMessage(SendCompletionCallback, payload, writeFlags, !InitialMetadataSent);

                    InitialMetadataSent = true;
                    streamingWritesCounter++;
                    return InitializeStreamingWrite();
                }
            }
        }

        /// <summary>
        /// Initiates reading a message. Only one read operation can be active at a time.
        /// </summary>
        protected Task<TRead> ReadMessageInternalAsync()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(Started);
                if (ReadingDone)
                {
                    // the last read that returns null or throws an exception is idempotent
                    // and maintains its state.
                    GrpcPreconditions.CheckState(StreamingReadInitialized, "Call does not support streaming reads.");
                    return streamingReadTmb.Task;
                }

                GrpcPreconditions.CheckState(!StreamingReadInitialized, "Only one read can be pending at a time");
                GrpcPreconditions.CheckState(!Disposed);

                call.StartReceiveMessage(ReceivedMessageCallback);
                streamingReadTmb = AsyncTaskMethodBuilder<TRead>.Create();
                SetState(StateFlags.StreamingReadInitialized, true);
                return streamingReadTmb.Task;
            }
        }

        /// <summary>
        /// If there are no more pending actions and no new actions can be started, releases
        /// the underlying native resources.
        /// </summary>
        protected bool ReleaseResourcesIfPossible()
        {
            if (!Disposed && call != null)
            {
                bool noMoreSendCompletions = !StreamingReadInitialized && HalfCloseRequested || CancelRequested || Finished;
                if (noMoreSendCompletions && ReadingDone && Finished && !ReceiveResponseHeadersPending)
                {
                    ReleaseResources();
                    return true;
                }
            }
            return false;
        }

        protected abstract bool IsClient
        {
            get;
        }

        /// <summary>
        /// Returns an exception to throw for a failed send operation.
        /// It is only allowed to call this method for a call that has already finished.
        /// </summary>
        protected abstract Exception GetRpcExceptionClientOnly();

        protected void ReleaseResources()
        {
            if (call != null)
            {
                call.Dispose();
            }
            Disposed = true;
            OnAfterReleaseResourcesLocked();
        }

        protected virtual void OnAfterReleaseResourcesLocked()
        {
        }

        protected virtual void OnAfterReleaseResourcesUnlocked()
        {
        }

        /// <summary>
        /// Checks if sending is allowed and possibly returns a Task that allows short-circuiting the send
        /// logic by directly returning the write operation result task. Normally, null is returned.
        /// </summary>
        protected abstract Task CheckSendAllowedOrEarlyResult();

        // runs the serializer, propagating any exceptions being thrown without modifying them
        protected SliceBufferSafeHandle UnsafeSerialize(TWrite msg, DefaultSerializationContext context)
        {
            serializer(msg, context);
            return context.GetPayload();
        }

        protected Exception TryDeserialize(IBufferReader reader, out TRead msg)
        {
            DefaultDeserializationContext context = null;
            try
            {
                context = DefaultDeserializationContext.GetInitializedThreadLocal(reader);
                msg = deserializer(context);
                return null;
            }
            catch (Exception e)
            {
                msg = default(TRead);
                return e;
            }
            finally
            {
                context?.Reset();
            }
        }

        /// <summary>
        /// Handles send completion (including SendCloseFromClient).
        /// </summary>
        protected void HandleSendFinished(bool success)
        {
            bool delayCompletion = false;
            AsyncTaskMethodBuilder origStreamingWrite = default;
            bool releasedResources;
            lock (myLock)
            {
                if (!success && !Finished && IsClient) {
                    // We should be setting this only once per call, following writes will be short circuited
                    // because they cannot start until the entire call finishes.
                    GrpcPreconditions.CheckState(!IsStreamingWriteCompletionDelayed);

                    // leave streamingWriteTcs set, it will be completed once call finished.
                    IsStreamingWriteCompletionDelayed = true;
                    delayCompletion = true;
                }
                else
                {
                    origStreamingWrite = ResetStreamingWrite().Value;
                }

                releasedResources = ReleaseResourcesIfPossible();
            }

            if (releasedResources)
            {
                OnAfterReleaseResourcesUnlocked();
            }

            if (!success)
            {
                if (!delayCompletion)
                {
                    if (IsClient)
                    {
                        GrpcPreconditions.CheckState(Finished);  // implied by !success && !delayCompletion && IsClient
                        origStreamingWrite.SetException(GetRpcExceptionClientOnly());
                    }
                    else
                    {
                        origStreamingWrite.SetException (new IOException("Error sending from server."));
                    }
                }
                // if delayCompletion == true, postpone SetException until call finishes.
            }
            else
            {
                origStreamingWrite.SetResult();
            }
        }

        /// <summary>
        /// Handles send status from server completion.
        /// </summary>
        protected void HandleSendStatusFromServerFinished(bool success)
        {
            bool releasedResources;
            lock (myLock)
            {
                releasedResources = ReleaseResourcesIfPossible();
            }

            if (releasedResources)
            {
                OnAfterReleaseResourcesUnlocked();
            }

            if (!success)
            {
                sendStatusFromServerTmb.SetException(new IOException("Error sending status from server."));
            }
            else
            {
                sendStatusFromServerTmb.SetResult();
            }
        }

        /// <summary>
        /// Handles streaming read completion.
        /// </summary>
        protected void HandleReadFinished(bool success, IBufferReader receivedMessageReader)
        {
            // if success == false, the message reader will report null payload. It that case we will
            // treat this completion as the last read an rely on C core to handle the failed
            // read (e.g. deliver approriate statusCode on the clientside).

            TRead msg = default(TRead);
            var deserializeException = (success && receivedMessageReader.TotalLength.HasValue) ? TryDeserialize(receivedMessageReader, out msg) : null;

            AsyncTaskMethodBuilder<TRead> origTcs = default;
            bool releasedResources;
            lock (myLock)
            {
                origTcs = streamingReadTmb;
                if (!receivedMessageReader.TotalLength.HasValue)
                {
                    // This was the last read.
                    ReadingDone = true;
                }

                if (deserializeException != null && IsClient)
                {
                    ReadingDone = true;

                    // TODO(jtattermusch): it might be too late to set the status
                    CancelWithStatus(DeserializeResponseFailureStatus);
                }

                if (!ReadingDone)
                {
                    SetState(StateFlags.StreamingReadInitialized, false);
                    streamingReadTmb = default;
                }

                releasedResources = ReleaseResourcesIfPossible();
            }

            if (releasedResources)
            {
                OnAfterReleaseResourcesUnlocked();
            }

            if (deserializeException != null && !IsClient)
            {
                origTcs.SetException(new IOException("Failed to deserialize request message.", deserializeException));
                return;
            }
            origTcs.SetResult(msg);
        }

        protected ISendCompletionCallback SendCompletionCallback => this;

        void ISendCompletionCallback.OnSendCompletion(bool success)
        {
            HandleSendFinished(success);
        }

        IReceivedMessageCallback ReceivedMessageCallback => this;

        void IReceivedMessageCallback.OnReceivedMessage(bool success, IBufferReader receivedMessageReader)
        {
            HandleReadFinished(success, receivedMessageReader);
        }

        internal CancellationTokenRegistration RegisterCancellationCallbackForToken(CancellationToken cancellationToken)
        {
            if (cancellationToken.CanBeCanceled) return cancellationToken.Register(CancelCallFromToken, this);
            return default(CancellationTokenRegistration);
        }

        private static readonly Action<object> CancelCallFromToken = state => ((AsyncCallBase<TWrite, TRead>)state).Cancel();
    }
}
