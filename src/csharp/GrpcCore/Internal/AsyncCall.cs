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
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.CompilerServices;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// Listener for call events that can be delivered from a completion queue.
    /// </summary>
    internal interface ICallEventListener {

        void OnClientMetadata();

        void OnRead(byte[] payload);

        void OnWriteAccepted(GRPCOpError error);

        void OnFinishAccepted(GRPCOpError error);

        // ignore the status on server
        void OnFinished(Status status);
    }

    /// <summary>
    /// Handle native call lifecycle and provides convenience methods.
    /// </summary>
    internal class AsyncCall<TWrite, TRead>: ICallEventListener, IDisposable
    {
        readonly Func<TWrite, byte[]> serializer;
        readonly Func<byte[], TRead> deserializer;

        // TODO: make sure the delegate doesn't get garbage collected while
        // native callbacks are in the completion queue.
        readonly EventCallbackDelegate callbackHandler;

        object myLock = new object();
        bool disposed;
        CallSafeHandle call;

        bool started;
        bool errorOccured;

        bool cancelRequested;
        bool halfcloseRequested;
        bool halfclosed;
        bool doneWithReading;
        Nullable<Status> finishedStatus;

        TaskCompletionSource<object> writeTcs;
        TaskCompletionSource<TRead> readTcs;
        TaskCompletionSource<object> halfcloseTcs = new TaskCompletionSource<object>();
        TaskCompletionSource<Status> finishedTcs = new TaskCompletionSource<Status>();

        IObserver<TRead> readObserver;

        public AsyncCall(Func<TWrite, byte[]> serializer, Func<byte[], TRead> deserializer)
        {
            this.serializer = serializer;
            this.deserializer = deserializer;
            this.callbackHandler = HandleEvent;
        }

        public Task WriteAsync(TWrite msg)
        {
            return StartWrite(msg, false).Task;
        }

        public Task WritesCompletedAsync()
        {
            WritesDone();
            return halfcloseTcs.Task;
        }

        public Task WriteStatusAsync(Status status)
        {
            WriteStatus(status);
            return halfcloseTcs.Task;
        }

        public Task<TRead> ReadAsync()
        {
            return StartRead().Task;
        }

        public Task Halfclosed
        {
            get
            {
                return halfcloseTcs.Task;
            }
        }

        public Task<Status> Finished
        {
            get
            {
                return finishedTcs.Task;
            }
        }

        /// <summary>
        /// Initiates reading to given observer.
        /// </summary>
        public void StartReadingToStream(IObserver<TRead> readObserver) {
            lock (myLock)
            {
                CheckStarted();
                if (this.readObserver != null)
                {
                    throw new InvalidOperationException("Already registered an observer.");
                }
                this.readObserver = readObserver;
                StartRead();
            }
        }

        public void Initialize(Channel channel, String methodName) {
            lock (myLock)
            {
               this.call = CallSafeHandle.Create(channel.Handle, methodName, channel.Target, Timespec.InfFuture);
            }
        }

        public void InitializeServer(CallSafeHandle call)
        {
            lock(myLock)
            {
                this.call = call;
            }
        }

        // Client only
        public void Start(bool buffered, CompletionQueueSafeHandle cq)
        {
            lock (myLock)
            {
                if (started)
                {
                    throw new InvalidOperationException("Already started.");
                }

                call.Invoke(cq, buffered, callbackHandler, callbackHandler);
                started = true;
            }
        }

        // Server only
        public void Accept(CompletionQueueSafeHandle cq)
        {
            lock (myLock)
            {
                if (started)
                {
                    throw new InvalidOperationException("Already started.");
                }

                call.ServerAccept(cq, callbackHandler);
                call.ServerEndInitialMetadata(0);
                started = true;
            }
        }

        public TaskCompletionSource<object> StartWrite(TWrite msg, bool buffered)
        {
            lock (myLock)
            {
                CheckStarted();
                CheckNotFinished();
                CheckNoError();
                CheckCancelNotRequested();

                if (halfcloseRequested || halfclosed)
                {
                    throw new InvalidOperationException("Already halfclosed.");
                }

                if (writeTcs != null)
                {
                    throw new InvalidOperationException("Only one write can be pending at a time");
                }

                // TODO: wrap serialization...
                byte[] payload = serializer(msg);

                call.StartWrite(payload, buffered, callbackHandler);
                writeTcs = new TaskCompletionSource<object>();
                return writeTcs;
            }
        }

        // client only
        public void WritesDone()
        {
            lock (myLock)
            {
                CheckStarted();
                CheckNotFinished();
                CheckNoError();
                CheckCancelNotRequested();

                if (halfcloseRequested || halfclosed)
                {
                    throw new InvalidOperationException("Already halfclosed.");
                }

                call.WritesDone(callbackHandler);
                halfcloseRequested = true;
            }
        }

        // server only
        public void WriteStatus(Status status)
        {
            lock (myLock)
            {
                CheckStarted();
                CheckNotFinished();
                CheckNoError();
                CheckCancelNotRequested();

                if (halfcloseRequested || halfclosed)
                {
                    throw new InvalidOperationException("Already halfclosed.");
                }

                call.StartWriteStatus(status, callbackHandler);
                halfcloseRequested = true;
            }
        }

        public TaskCompletionSource<TRead> StartRead()
        {
            lock (myLock)
            {
                CheckStarted();
                CheckNotFinished();
                CheckNoError();

                // TODO: add check for not cancelled?

                if (doneWithReading)
                {
                    throw new InvalidOperationException("Already read the last message.");
                }

                if (readTcs != null)
                {
                    throw new InvalidOperationException("Only one read can be pending at a time");
                }

                call.StartRead(callbackHandler);

                readTcs = new TaskCompletionSource<TRead>();
                return readTcs;
            }
        }

        public void Cancel()
        {
            lock (myLock)
            {
                CheckStarted();
                CheckNotFinished();

                cancelRequested = true;
            }
            // grpc_call_cancel is threadsafe
            call.Cancel();
        }

        public void CancelWithStatus(Status status)
        {
            lock (myLock)
            {
                CheckStarted();
                CheckNotFinished();

                cancelRequested = true;
            }
            // grpc_call_cancel_with_status is threadsafe
            call.CancelWithStatus(status);
        }

        public void OnClientMetadata()
        {
            // TODO: implement....
        }

        public void OnRead(byte[] payload)
        {
            TaskCompletionSource<TRead> oldTcs = null;
            IObserver<TRead> observer = null;
            lock (myLock)
            {
                oldTcs = readTcs;
                readTcs = null;
                if (payload == null)
                {
                    doneWithReading = true;
                }
                observer = readObserver;
            }

            // TODO: wrap deserialization...
            TRead msg = payload != null ? deserializer(payload) : default(TRead);

            oldTcs.SetResult(msg);

            // TODO: make sure we deliver reads in the right order.

            if (observer != null)
            {
                if (payload != null)
                {
                    // TODO: wrap to handle exceptions
                    observer.OnNext(msg);

                    // start a new read
                    StartRead();
                }
                else
                {
                    // TODO: wrap to handle exceptions;
                    observer.OnCompleted();
                }

            }
        }

        public void OnWriteAccepted(GRPCOpError error)
        {
            TaskCompletionSource<object> oldTcs = null;
            lock (myLock)
            {
                UpdateErrorOccured(error);
                oldTcs = writeTcs;
                writeTcs = null;
            }

            if (errorOccured)
            {
                // TODO: use the right type of exception...
                oldTcs.SetException(new Exception("Write failed"));
            }
            else
            {
                // TODO: where does the continuation run?
                oldTcs.SetResult(null);
            }
        }

        public void OnFinishAccepted(GRPCOpError error)
        {
            lock (myLock)
            {
                UpdateErrorOccured(error);
                halfclosed = true;
            }

            if (errorOccured)
            {
                halfcloseTcs.SetException(new Exception("Halfclose failed"));

            }
            else
            {
                halfcloseTcs.SetResult(null);
            }

        }

        public void OnFinished(Status status)
        {
            lock (myLock)
            {
                finishedStatus = status;

                DisposeResourcesIfNeeded();
            }
            finishedTcs.SetResult(status);

        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!disposed)
            {
                if (disposing)
                {
                    if (call != null)
                    {
                        call.Dispose();
                    }
                }
                disposed = true;
            }
        }

        private void UpdateErrorOccured(GRPCOpError error)
        {
            if (error == GRPCOpError.GRPC_OP_ERROR)
            {
                errorOccured = true;
            }
        }

        private void CheckStarted()
        {
            if (!started)
            {
                throw new InvalidOperationException("Call not started");
            }
        }

        private void CheckNoError()
        {
            if (errorOccured)
            {
                throw new InvalidOperationException("Error occured when processing call.");
            }
        }

        private void CheckNotFinished()
        {
            if (finishedStatus.HasValue)
            {
                throw new InvalidOperationException("Already finished.");
            }
        }

        private void CheckCancelNotRequested()
        {
            if (cancelRequested)
            {
                throw new InvalidOperationException("Cancel has been requested.");
            }
        }

        private void DisposeResourcesIfNeeded()
        {
            if (call != null && started && finishedStatus.HasValue)
            {
                // TODO: should we also wait for all the pending events to finish?

                call.Dispose();
            }
        }

        private void HandleEvent(IntPtr eventPtr) {
            try {
                var ev = new EventSafeHandleNotOwned(eventPtr);
                switch (ev.GetCompletionType())
                {
                case GRPCCompletionType.GRPC_CLIENT_METADATA_READ:
                    OnClientMetadata();
                    break;

                case GRPCCompletionType.GRPC_READ:
                    byte[] payload = ev.GetReadData();
                    OnRead(payload);
                    break;

                case GRPCCompletionType.GRPC_WRITE_ACCEPTED:
                    OnWriteAccepted(ev.GetWriteAccepted());
                    break;

                case GRPCCompletionType.GRPC_FINISH_ACCEPTED:
                    OnFinishAccepted(ev.GetFinishAccepted());
                    break;

                case GRPCCompletionType.GRPC_FINISHED:
                    OnFinished(ev.GetFinished());
                    break;

                default:
                    throw new ArgumentException("Unexpected completion type");
                }
            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }
    }
}
