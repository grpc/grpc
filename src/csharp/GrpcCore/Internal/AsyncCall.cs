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
    /// Handle native call lifecycle and provides convenience methods.
    /// </summary>
    internal class AsyncCall<TWrite, TRead> : IDisposable
    {
        readonly Func<TWrite, byte[]> serializer;
        readonly Func<byte[], TRead> deserializer;

        // TODO: make sure the delegate doesn't get garbage collected while 
        // native callbacks are in the completion queue.
        readonly CompletionCallbackDelegate unaryResponseHandler;
        readonly CompletionCallbackDelegate finishedHandler;
        readonly CompletionCallbackDelegate writeFinishedHandler;
        readonly CompletionCallbackDelegate readFinishedHandler;
        readonly CompletionCallbackDelegate halfclosedHandler;
        readonly CompletionCallbackDelegate finishedServersideHandler;

        object myLock = new object();
        bool disposed;
        CallSafeHandle call;

        bool server;
        bool started;
        bool errorOccured;

        bool cancelRequested;
        bool halfcloseRequested;
        bool halfclosed;
        bool doneWithReading;
        Nullable<Status> finishedStatus;

        TaskCompletionSource<object> writeTcs;
        TaskCompletionSource<TRead> readTcs;

        TaskCompletionSource<object> finishedServersideTcs = new TaskCompletionSource<object>();
        TaskCompletionSource<object> halfcloseTcs = new TaskCompletionSource<object>();
        TaskCompletionSource<Status> finishedTcs = new TaskCompletionSource<Status>();

        TaskCompletionSource<TRead> unaryResponseTcs;

        IObserver<TRead> readObserver;

        public AsyncCall(Func<TWrite, byte[]> serializer, Func<byte[], TRead> deserializer)
        {
            this.serializer = serializer;
            this.deserializer = deserializer;
            this.unaryResponseHandler = HandleUnaryResponseCompletion;
            this.finishedHandler = HandleFinished;
            this.writeFinishedHandler = HandleWriteFinished;
            this.readFinishedHandler = HandleReadFinished;
            this.halfclosedHandler = HandleHalfclosed;
            this.finishedServersideHandler = HandleFinishedServerside;
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
                ReceiveMessageAsync();
            }
        }

        public void Initialize(Channel channel, CompletionQueueSafeHandle cq, String methodName) {
            lock (myLock)
            {
                this.call = CallSafeHandle.Create(channel.Handle, cq, methodName, channel.Target, Timespec.InfFuture);
            }
        }

        public void InitializeServer(CallSafeHandle call)
        {
            lock(myLock)
            {
                this.call = call;
                started = true;
                server = true;
            }
        }


        public Task<TRead> UnaryCallAsync(TWrite msg)
        {
            lock (myLock)
            {
                started = true;
                halfcloseRequested = true;

                // TODO: handle serialization error...
                byte[] payload = serializer(msg);

                unaryResponseTcs = new TaskCompletionSource<TRead>();
                call.StartUnary(payload, unaryResponseHandler);

                return unaryResponseTcs.Task;
            }
        }

        public Task<TRead> ClientStreamingCallAsync()
        {
            lock (myLock)
            {
                started = true;

                unaryResponseTcs = new TaskCompletionSource<TRead>();
                call.StartClientStreaming(unaryResponseHandler);

                return unaryResponseTcs.Task;
            }
        }

        public void StartServerStreamingCall(TWrite msg, IObserver<TRead> readObserver)
        {
            lock (myLock)
            {
                started = true;
                halfcloseRequested = true;
        
                this.readObserver = readObserver;

                // TODO: handle serialization error...
                byte[] payload = serializer(msg);
        
                call.StartServerStreaming(payload, finishedHandler);

                ReceiveMessageAsync();
            }
        }

        public void StartDuplexStreamingCall(IObserver<TRead> readObserver)
        {
            lock (myLock)
            {
                started = true;

                this.readObserver = readObserver;

                call.StartDuplexStreaming(finishedHandler);

                ReceiveMessageAsync();
            }
        }

        public Task SendMessageAsync(TWrite msg) {
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

                call.StartSendMessage(payload, writeFinishedHandler);
                writeTcs = new TaskCompletionSource<object>();
                return writeTcs.Task;
            }
        }

        public Task SendCloseFromClientAsync()
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

                call.StartSendCloseFromClient(halfclosedHandler);
                halfcloseRequested = true;
                return halfcloseTcs.Task;
            }
        }

        public Task SendStatusFromServerAsync(Status status)
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

                call.StartSendStatusFromServer(status, halfclosedHandler);
                halfcloseRequested = true;
                return halfcloseTcs.Task;
            }
        }

        public Task<TRead> ReceiveMessageAsync()
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

                call.StartReceiveMessage(readFinishedHandler);

                readTcs = new TaskCompletionSource<TRead>();
                return readTcs.Task;
            }
        }

        internal Task StartServerSide()
        {
            lock (myLock)
            {
                call.StartServerSide(finishedServersideHandler);
                return finishedServersideTcs.Task;
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

        private void CompleteStreamObserver(Status status) {
            if (status.StatusCode != StatusCode.GRPC_STATUS_OK)
            {
                // TODO: wrap to handle exceptions;
                readObserver.OnError(new RpcException(status));
            } else {
                // TODO: wrap to handle exceptions;
                readObserver.OnCompleted();
            }
        }

        private void HandleUnaryResponseCompletion(GRPCOpError error, IntPtr batchContextPtr) {
            try {

                TaskCompletionSource<TRead> tcs;
                lock(myLock) {
                    tcs = unaryResponseTcs;
                }

                // we're done with this call, get rid of the native object.
                call.Dispose();

                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);

                if (error != GRPCOpError.GRPC_OP_OK) {
                    tcs.SetException(new RpcException(
                        new Status(StatusCode.GRPC_STATUS_INTERNAL, "Internal error occured.")
                    ));
                    return;
                }

                var status = ctx.GetReceivedStatus();
                if (status.StatusCode != StatusCode.GRPC_STATUS_OK) {
                    tcs.SetException(new RpcException(status));
                    return;
                }

                // TODO: handle deserialize error...
                var msg = deserializer(ctx.GetReceivedMessage());
                tcs.SetResult(msg);
            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleWriteFinished(GRPCOpError error, IntPtr batchContextPtr) {
            try {

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

            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleHalfclosed(GRPCOpError error, IntPtr batchContextPtr) {
            try {
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
            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleReadFinished(GRPCOpError error, IntPtr batchContextPtr) {
            try {

                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);
                var payload = ctx.GetReceivedMessage();

                TaskCompletionSource<TRead> oldTcs = null;
                IObserver<TRead> observer = null;

                Nullable<Status> status = null;

                lock (myLock)
                {
                    oldTcs = readTcs;
                    readTcs = null;
                    if (payload == null)
                    {
                        doneWithReading = true;
                    }
                    observer = readObserver;
                    status = finishedStatus;
                }

                // TODO: wrap deserialization...
                TRead msg = payload != null ? deserializer(payload) : default(TRead);

                oldTcs.SetResult(msg);

                // TODO: make sure we deliver reads in the right order.

                if (observer != null) {
                    if (payload != null)
                    {
                        // TODO: wrap to handle exceptions
                        observer.OnNext(msg);

                        // start a new read
                        ReceiveMessageAsync();
                    }
                    else
                    {
                        if (!server) {
                            if (status.HasValue) {
                                CompleteStreamObserver(status.Value);
                            }
                        } else {
                            // TODO: wrap to handle exceptions..
                            observer.OnCompleted();
                        }
                        // TODO: completeStreamObserver serverside...
                    }
               }
            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleFinished(GRPCOpError error, IntPtr batchContextPtr) {
            try {
                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);
                var status = ctx.GetReceivedStatus();

                bool wasDoneWithReading;

                lock (myLock)
                {
                    finishedStatus = status;

                    DisposeResourcesIfNeeded();

                    wasDoneWithReading = doneWithReading;
                }

                if (wasDoneWithReading) {
                    CompleteStreamObserver(status);
                }

            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleFinishedServerside(GRPCOpError error, IntPtr batchContextPtr) {
            try {
                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);

                // TODO: handle error ...

                finishedServersideTcs.SetResult(null);

                call.Dispose();

            } catch(Exception e) {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }
    }
}