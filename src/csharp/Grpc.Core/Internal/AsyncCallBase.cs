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

        protected readonly GrpcEnvironment environment;
        protected readonly object myLock = new object();

        protected INativeCall call;
        protected bool disposed;

        protected bool started;
        protected bool cancelRequested;

        protected AsyncCompletionDelegate<object> sendCompletionDelegate;  // Completion of a pending send or sendclose if not null.
        protected AsyncCompletionDelegate<TRead> readCompletionDelegate;  // Completion of a pending send or sendclose if not null.

        protected bool readingDone;  // True if last read (i.e. read with null payload) was already received.
        protected bool halfcloseRequested;  // True if send close have been initiated.
        protected bool finished;  // True if close has been received from the peer.

        protected bool initialMetadataSent;
        protected long streamingWritesCounter;  // Number of streaming send operations started so far.

        public AsyncCallBase(Func<TWrite, byte[]> serializer, Func<byte[], TRead> deserializer, GrpcEnvironment environment)
        {
            this.serializer = Preconditions.CheckNotNull(serializer);
            this.deserializer = Preconditions.CheckNotNull(deserializer);
            this.environment = Preconditions.CheckNotNull(environment);
        }

        /// <summary>
        /// Requests cancelling the call.
        /// </summary>
        public void Cancel()
        {
            lock (myLock)
            {
                Preconditions.CheckState(started);
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
        /// completionDelegate is invoked upon completion.
        /// </summary>
        protected void StartSendMessageInternal(TWrite msg, WriteFlags writeFlags, AsyncCompletionDelegate<object> completionDelegate)
        {
            byte[] payload = UnsafeSerialize(msg);

            lock (myLock)
            {
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");
                CheckSendingAllowed();

                call.StartSendMessage(HandleSendFinished, payload, writeFlags, !initialMetadataSent);

                sendCompletionDelegate = completionDelegate;
                initialMetadataSent = true;
                streamingWritesCounter++;
            }
        }

        /// <summary>
        /// Initiates reading a message. Only one read operation can be active at a time.
        /// completionDelegate is invoked upon completion.
        /// </summary>
        protected void StartReadMessageInternal(AsyncCompletionDelegate<TRead> completionDelegate)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");
                CheckReadingAllowed();

                call.StartReceiveMessage(HandleReadFinished);
                readCompletionDelegate = completionDelegate;
            }
        }

        /// <summary>
        /// If there are no more pending actions and no new actions can be started, releases
        /// the underlying native resources.
        /// </summary>
        protected bool ReleaseResourcesIfPossible()
        {
            using (Profilers.ForCurrentThread().NewScope("AsyncCallBase.ReleaseResourcesIfPossible"))
            {
                if (!disposed && call != null)
                {
                    bool noMoreSendCompletions = sendCompletionDelegate == null && (halfcloseRequested || cancelRequested || finished);
                    if (noMoreSendCompletions && readingDone && finished)
                    {
                        ReleaseResources();
                        return true;
                    }
                }
                return false;
            }
        }

        protected abstract bool IsClient
        {
            get;
        }

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

        protected void CheckSendingAllowed()
        {
            Preconditions.CheckState(started);
            CheckNotCancelled();
            Preconditions.CheckState(!disposed);

            Preconditions.CheckState(!halfcloseRequested, "Already halfclosed.");
            Preconditions.CheckState(!finished, "Already finished.");
            Preconditions.CheckState(sendCompletionDelegate == null, "Only one write can be pending at a time");
        }

        protected virtual void CheckReadingAllowed()
        {
            Preconditions.CheckState(started);
            Preconditions.CheckState(!disposed);

            Preconditions.CheckState(!readingDone, "Stream has already been closed.");
            Preconditions.CheckState(readCompletionDelegate == null, "Only one read can be pending at a time");
        }

        protected void CheckNotCancelled()
        {
            if (cancelRequested)
            {
                throw new OperationCanceledException("Remote call has been cancelled.");
            }
        }

        protected byte[] UnsafeSerialize(TWrite msg)
        {
            using (Profilers.ForCurrentThread().NewScope("AsyncCallBase.UnsafeSerialize"))
            {
                return serializer(msg);
            }
        }

        protected Exception TrySerialize(TWrite msg, out byte[] payload)
        {
            try
            {
                payload = serializer(msg);
                return null;
            }
            catch (Exception e)
            {
                payload = null;
                return e;
            }
        }

        protected Exception TryDeserialize(byte[] payload, out TRead msg)
        {
            using (Profilers.ForCurrentThread().NewScope("AsyncCallBase.TryDeserialize"))
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
        }

        protected void FireCompletion<T>(AsyncCompletionDelegate<T> completionDelegate, T value, Exception error)
        {
            try
            {
                completionDelegate(value, error);
            }
            catch (Exception e)
            {
                Logger.Error(e, "Exception occured while invoking completion delegate.");
            }
        }

        /// <summary>
        /// Handles send completion.
        /// </summary>
        protected void HandleSendFinished(bool success)
        {
            AsyncCompletionDelegate<object> origCompletionDelegate = null;
            lock (myLock)
            {
                origCompletionDelegate = sendCompletionDelegate;
                sendCompletionDelegate = null;

                ReleaseResourcesIfPossible();
            }

            if (!success)
            {
                FireCompletion(origCompletionDelegate, null, new InvalidOperationException("Send failed"));
            }
            else
            {
                FireCompletion(origCompletionDelegate, null, null);
            }
        }

        /// <summary>
        /// Handles halfclose completion.
        /// </summary>
        protected void HandleHalfclosed(bool success)
        {
            AsyncCompletionDelegate<object> origCompletionDelegate = null;
            lock (myLock)
            {
                origCompletionDelegate = sendCompletionDelegate;
                sendCompletionDelegate = null;

                ReleaseResourcesIfPossible();
            }

            if (!success)
            {
                FireCompletion(origCompletionDelegate, null, new InvalidOperationException("Halfclose failed"));
            }
            else
            {
                FireCompletion(origCompletionDelegate, null, null);
            }
        }

        /// <summary>
        /// Handles streaming read completion.
        /// </summary>
        protected void HandleReadFinished(bool success, byte[] receivedMessage)
        {
            TRead msg = default(TRead);
            var deserializeException = (success && receivedMessage != null) ? TryDeserialize(receivedMessage, out msg) : null;

            AsyncCompletionDelegate<TRead> origCompletionDelegate = null;
            lock (myLock)
            {
                origCompletionDelegate = readCompletionDelegate;
                readCompletionDelegate = null;

                if (receivedMessage == null)
                {
                    // This was the last read.
                    readingDone = true;
                }

                if (deserializeException != null && IsClient)
                {
                    readingDone = true;
                    CancelWithStatus(DeserializeResponseFailureStatus);
                }

                ReleaseResourcesIfPossible();
            }

            // TODO: handle the case when success==false

            if (deserializeException != null && !IsClient)
            {
                FireCompletion(origCompletionDelegate, default(TRead), new IOException("Failed to deserialize request message.", deserializeException));
                return;
            }
            FireCompletion(origCompletionDelegate, msg, null);
        }
    }
}