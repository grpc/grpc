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
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Base for handling both client side and server side calls.
    /// Manages native call lifecycle and provides convenience methods.
    /// </summary>
    internal abstract class AsyncCallBase<TWrite, TRead>
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<AsyncCallBase<TWrite, TRead>>();
        protected static readonly Status DeserializeResponseFailureStatus = new Status(StatusCode.Internal, "Failed to deserialize response message.");

        readonly Func<TWrite, byte[]> serializer;
        readonly Func<byte[], TRead> deserializer;

        protected readonly object myLock = new object();

        protected INativeCall call;
        protected bool disposed;

        protected bool started;
        protected bool cancelRequested;

        protected TaskCompletionSource<TRead> streamingReadTcs;  // Completion of a pending streaming read if not null.
        protected TaskCompletionSource<object> streamingWriteTcs;  // Completion of a pending streaming write or send close from client if not null.
        protected TaskCompletionSource<object> sendStatusFromServerTcs;
        protected bool isStreamingWriteCompletionDelayed;  // Only used for the client side.

        protected bool readingDone;  // True if last read (i.e. read with null payload) was already received.
        protected bool halfcloseRequested;  // True if send close have been initiated.
        protected bool finished;  // True if close has been received from the peer.

        protected bool initialMetadataSent;
        protected long streamingWritesCounter;  // Number of streaming send operations started so far.

        public AsyncCallBase(Func<TWrite, byte[]> serializer, Func<byte[], TRead> deserializer)
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
                GrpcPreconditions.CheckState(started);
                cancelRequested = true;

                if (!disposed)
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
                cancelRequested = true;

                if (!disposed)
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
            byte[] payload = UnsafeSerialize(msg);

            lock (myLock)
            {
                GrpcPreconditions.CheckState(started);
                var earlyResult = CheckSendAllowedOrEarlyResult();
                if (earlyResult != null)
                {
                    return earlyResult;
                }

                call.StartSendMessage(HandleSendFinished, payload, writeFlags, !initialMetadataSent);

                initialMetadataSent = true;
                streamingWritesCounter++;
                streamingWriteTcs = new TaskCompletionSource<object>();
                return streamingWriteTcs.Task;
            }
        }

        /// <summary>
        /// Initiates reading a message. Only one read operation can be active at a time.
        /// </summary>
        protected Task<TRead> ReadMessageInternalAsync()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(started);
                if (readingDone)
                {
                    // the last read that returns null or throws an exception is idempotent
                    // and maintains its state.
                    GrpcPreconditions.CheckState(streamingReadTcs != null, "Call does not support streaming reads.");
                    return streamingReadTcs.Task;
                }

                GrpcPreconditions.CheckState(streamingReadTcs == null, "Only one read can be pending at a time");
                GrpcPreconditions.CheckState(!disposed);

                call.StartReceiveMessage(HandleReadFinished);
                streamingReadTcs = new TaskCompletionSource<TRead>();
                return streamingReadTcs.Task;
            }
        }

        /// <summary>
        /// If there are no more pending actions and no new actions can be started, releases
        /// the underlying native resources.
        /// </summary>
        protected bool ReleaseResourcesIfPossible()
        {
            if (!disposed && call != null)
            {
                bool noMoreSendCompletions = streamingWriteTcs == null && (halfcloseRequested || cancelRequested || finished);
                if (noMoreSendCompletions && readingDone && finished)
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

        private void ReleaseResources()
        {
            if (call != null)
            {
                call.Dispose();
            }
            disposed = true;
            OnAfterReleaseResources();
        }

        protected virtual void OnAfterReleaseResources()
        {
        }

        /// <summary>
        /// Checks if sending is allowed and possibly returns a Task that allows short-circuiting the send
        /// logic by directly returning the write operation result task. Normally, null is returned.
        /// </summary>
        protected abstract Task CheckSendAllowedOrEarlyResult();

        protected byte[] UnsafeSerialize(TWrite msg)
        {
            return serializer(msg);
        }

        protected Exception TryDeserialize(byte[] payload, out TRead msg)
        {
            try
            {
                msg = deserializer(payload);
                return null;
            }
            catch (Exception e)
            {
                msg = default(TRead);
                return e;
            }
        }

        /// <summary>
        /// Handles send completion (including SendCloseFromClient).
        /// </summary>
        protected void HandleSendFinished(bool success)
        {
            bool delayCompletion = false;
            TaskCompletionSource<object> origTcs = null;
            lock (myLock)
            {
                if (!success && !finished && IsClient) {
                    // We should be setting this only once per call, following writes will be short circuited
                    // because they cannot start until the entire call finishes.
                    GrpcPreconditions.CheckState(!isStreamingWriteCompletionDelayed);

                    // leave streamingWriteTcs set, it will be completed once call finished.
                    isStreamingWriteCompletionDelayed = true;
                    delayCompletion = true;
                }
                else
                {
                    origTcs = streamingWriteTcs;
                    streamingWriteTcs = null;    
                }

                ReleaseResourcesIfPossible();
            }

            if (!success)
            {
                if (!delayCompletion)
                {
                    if (IsClient)
                    {
                        GrpcPreconditions.CheckState(finished);  // implied by !success && !delayCompletion && IsClient
                        origTcs.SetException(GetRpcExceptionClientOnly());
                    }
                    else
                    {
                        origTcs.SetException (new IOException("Error sending from server."));
                    }
                }
                // if delayCompletion == true, postpone SetException until call finishes.
            }
            else
            {
                origTcs.SetResult(null);
            }
        }

        /// <summary>
        /// Handles send status from server completion.
        /// </summary>
        protected void HandleSendStatusFromServerFinished(bool success)
        {
            lock (myLock)
            {
                ReleaseResourcesIfPossible();
            }

            if (!success)
            {
                sendStatusFromServerTcs.SetException(new IOException("Error sending status from server."));
            }
            else
            {
                sendStatusFromServerTcs.SetResult(null);
            }
        }

        /// <summary>
        /// Handles streaming read completion.
        /// </summary>
        protected void HandleReadFinished(bool success, byte[] receivedMessage)
        {
            // if success == false, received message will be null. It that case we will
            // treat this completion as the last read an rely on C core to handle the failed
            // read (e.g. deliver approriate statusCode on the clientside).

            TRead msg = default(TRead);
            var deserializeException = (success && receivedMessage != null) ? TryDeserialize(receivedMessage, out msg) : null;

            TaskCompletionSource<TRead> origTcs = null;
            lock (myLock)
            {
                origTcs = streamingReadTcs;
                if (receivedMessage == null)
                {
                    // This was the last read.
                    readingDone = true;
                }

                if (deserializeException != null && IsClient)
                {
                    readingDone = true;

                    // TODO(jtattermusch): it might be too late to set the status
                    CancelWithStatus(DeserializeResponseFailureStatus);
                }

                if (!readingDone)
                {
                    streamingReadTcs = null;
                }

                ReleaseResourcesIfPossible();
            }

            if (deserializeException != null && !IsClient)
            {
                origTcs.SetException(new IOException("Failed to deserialize request message.", deserializeException));
                return;
            }
            origTcs.SetResult(msg);
        }
    }
}
