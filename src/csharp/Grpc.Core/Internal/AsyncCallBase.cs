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
    /// Base for handling both client side and server side calls.
    /// Manages native call lifecycle and provides convenience methods.
    /// </summary>
    internal abstract class AsyncCallBase<TWrite, TRead>
    {
        readonly Func<TWrite, byte[]> serializer;
        readonly Func<byte[], TRead> deserializer;

        protected readonly CompletionCallbackDelegate sendFinishedHandler;
        protected readonly CompletionCallbackDelegate readFinishedHandler;
        protected readonly CompletionCallbackDelegate halfclosedHandler;

        protected readonly object myLock = new object();

        protected GCHandle gchandle;
        protected CallSafeHandle call;
        protected bool disposed;

        protected bool started;
        protected bool errorOccured;
        protected bool cancelRequested;

        protected AsyncCompletionDelegate<object> sendCompletionDelegate;  // Completion of a pending send or sendclose if not null.
        protected AsyncCompletionDelegate<TRead> readCompletionDelegate;  // Completion of a pending send or sendclose if not null.

        protected bool readingDone;
        protected bool halfcloseRequested;
        protected bool halfclosed;
        protected bool finished;  // True if close has been received from the peer.

        public AsyncCallBase(Func<TWrite, byte[]> serializer, Func<byte[], TRead> deserializer)
        {
            this.serializer = Preconditions.CheckNotNull(serializer);
            this.deserializer = Preconditions.CheckNotNull(deserializer);
  
            this.sendFinishedHandler = CreateBatchCompletionCallback(HandleSendFinished);
            this.readFinishedHandler = CreateBatchCompletionCallback(HandleReadFinished);
            this.halfclosedHandler = CreateBatchCompletionCallback(HandleHalfclosed);
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
        public void CancelWithStatus(Status status)
        {
            lock (myLock)
            {
                Preconditions.CheckState(started);
                cancelRequested = true;

                if (!disposed)
                {
                    call.CancelWithStatus(status);
                }
            }
        }

        protected void InitializeInternal(CallSafeHandle call)
        {
            lock (myLock)
            {
                // Make sure this object and the delegated held by it will not be garbage collected
                // before we release this handle.
                gchandle = GCHandle.Alloc(this);
                this.call = call;
            }
        }

        /// <summary>
        /// Initiates sending a message. Only one send operation can be active at a time.
        /// completionDelegate is invoked upon completion.
        /// </summary>
        protected void StartSendMessageInternal(TWrite msg, AsyncCompletionDelegate<object> completionDelegate)
        {
            byte[] payload = UnsafeSerialize(msg);

            lock (myLock)
            {
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");
                CheckSendingAllowed();

                call.StartSendMessage(payload, sendFinishedHandler);
                sendCompletionDelegate = completionDelegate;
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

                call.StartReceiveMessage(readFinishedHandler);
                readCompletionDelegate = completionDelegate;
            }
        }

        // TODO(jtattermusch): find more fitting name for this method.
        /// <summary>
        /// Default behavior just completes the read observer, but more sofisticated behavior might be required
        /// by subclasses.
        /// </summary>
        protected virtual void ProcessLastRead(AsyncCompletionDelegate<TRead> completionDelegate)
        {
            FireCompletion(completionDelegate, default(TRead), null);
        }

        /// <summary>
        /// If there are no more pending actions and no new actions can be started, releases
        /// the underlying native resources.
        /// </summary>
        protected bool ReleaseResourcesIfPossible()
        {
            if (!disposed && call != null)
            {
                bool noMoreSendCompletions = halfclosed || (cancelRequested && sendCompletionDelegate == null);
                if (noMoreSendCompletions && readingDone && finished)
                {
                    ReleaseResources();
                    return true;
                }
            }
            return false;
        }

        private void ReleaseResources()
        {
            OnReleaseResources();
            if (call != null)
            {
                call.Dispose();
            }
            gchandle.Free();
            disposed = true;
        }

        protected virtual void OnReleaseResources()
        {
        }

        protected void CheckSendingAllowed()
        {
            Preconditions.CheckState(started);
            Preconditions.CheckState(!errorOccured);
            CheckNotCancelled();
            Preconditions.CheckState(!disposed);

            Preconditions.CheckState(!halfcloseRequested, "Already halfclosed.");
            Preconditions.CheckState(sendCompletionDelegate == null, "Only one write can be pending at a time");
        }

        protected void CheckReadingAllowed()
        {
            Preconditions.CheckState(started);
            Preconditions.CheckState(!disposed);
            Preconditions.CheckState(!errorOccured);

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
            return serializer(msg);
        }

        protected bool TrySerialize(TWrite msg, out byte[] payload)
        {
            try
            {
                payload = serializer(msg);
                return true;
            }
            catch (Exception)
            {
                Console.WriteLine("Exception occured while trying to serialize message");
                payload = null;
                return false;
            }
        }

        protected bool TryDeserialize(byte[] payload, out TRead msg)
        {
            try
            {
                msg = deserializer(payload);
                return true;
            } 
            catch (Exception)
            {
                Console.WriteLine("Exception occured while trying to deserialize message");
                msg = default(TRead);
                return false;
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
                Console.WriteLine("Exception occured while invoking completion delegate: " + e);
            }
        }

        /// <summary>
        /// Creates completion callback delegate that wraps the batch completion handler in a try catch block to
        /// prevent propagating exceptions accross managed/unmanaged boundary.
        /// </summary>
        protected CompletionCallbackDelegate CreateBatchCompletionCallback(Action<bool, BatchContextSafeHandleNotOwned> handler)
        {
            return new CompletionCallbackDelegate((error, batchContextPtr) =>
            {
                try
                {
                    var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);
                    bool wasError = (error != GRPCOpError.GRPC_OP_OK);
                    handler(wasError, ctx);
                }
                catch (Exception e)
                {
                    Console.WriteLine("Caught exception in a native handler: " + e);
                }
            });
        }

        /// <summary>
        /// Handles send completion.
        /// </summary>
        private void HandleSendFinished(bool wasError, BatchContextSafeHandleNotOwned ctx)
        {
            AsyncCompletionDelegate<object> origCompletionDelegate = null;
            lock (myLock)
            {
                origCompletionDelegate = sendCompletionDelegate;
                sendCompletionDelegate = null;

                ReleaseResourcesIfPossible();
            }

            if (wasError)
            {
                FireCompletion(origCompletionDelegate, null, new OperationFailedException("Send failed"));
            }
            else
            {
                FireCompletion(origCompletionDelegate, null, null);
            }
        }

        /// <summary>
        /// Handles halfclose completion.
        /// </summary>
        private void HandleHalfclosed(bool wasError, BatchContextSafeHandleNotOwned ctx)
        {
            AsyncCompletionDelegate<object> origCompletionDelegate = null;
            lock (myLock)
            {
                halfclosed = true;
                origCompletionDelegate = sendCompletionDelegate;
                sendCompletionDelegate = null;

                ReleaseResourcesIfPossible();
            }

            if (wasError)
            {
                FireCompletion(origCompletionDelegate, null, new OperationFailedException("Halfclose failed"));
            }
            else
            {
                FireCompletion(origCompletionDelegate, null, null);
            }
        }

        /// <summary>
        /// Handles streaming read completion.
        /// </summary>
        private void HandleReadFinished(bool wasError, BatchContextSafeHandleNotOwned ctx)
        {
            var payload = ctx.GetReceivedMessage();

            AsyncCompletionDelegate<TRead> origCompletionDelegate = null;
            lock (myLock)
            {
                origCompletionDelegate = readCompletionDelegate;
                if (payload != null)
                {
                    readCompletionDelegate = null;
                }
                else
                {
                    // This was the last read. Keeping the readCompletionDelegate
                    // to be either fired by this handler or by client-side finished
                    // handler.
                    readingDone = true;
                }

                ReleaseResourcesIfPossible();
            }

            // TODO: handle the case when error occured...

            if (payload != null)
            {
                // TODO: handle deserialization error
                TRead msg;
                TryDeserialize(payload, out msg);

                FireCompletion(origCompletionDelegate, msg, null);
            }
            else
            {
                ProcessLastRead(origCompletionDelegate);
            }
        }
    }
}