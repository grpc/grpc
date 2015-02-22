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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Handles native call lifecycle and provides convenience methods.
    /// </summary>
    internal class AsyncCall<TWrite, TRead>
    {
        readonly Func<TWrite, byte[]> serializer;
        readonly Func<byte[], TRead> deserializer;

        readonly CompletionCallbackDelegate unaryResponseHandler;
        readonly CompletionCallbackDelegate finishedHandler;
        readonly CompletionCallbackDelegate writeFinishedHandler;
        readonly CompletionCallbackDelegate readFinishedHandler;
        readonly CompletionCallbackDelegate halfclosedHandler;
        readonly CompletionCallbackDelegate finishedServersideHandler;

        object myLock = new object();
        GCHandle gchandle;
        CallSafeHandle call;
        bool disposed;

        bool server;

        bool started;
        bool errorOccured;
        bool cancelRequested;
        bool readingDone;
        bool halfcloseRequested;
        bool halfclosed;
        bool finished;

        // Completion of a pending write if not null.
        TaskCompletionSource<object> writeTcs;

        // Completion of a pending read if not null.
        TaskCompletionSource<TRead> readTcs;

        // Completion of a pending halfclose if not null.
        TaskCompletionSource<object> halfcloseTcs;

        // Completion of a pending unary response if not null.
        TaskCompletionSource<TRead> unaryResponseTcs;

        // Set after status is received on client. Only used for server streaming and duplex streaming calls.
        Nullable<Status> finishedStatus;
        TaskCompletionSource<object> finishedServersideTcs = new TaskCompletionSource<object>();

        // For streaming, the reads will be delivered to this observer.
        IObserver<TRead> readObserver;

        public AsyncCall(Func<TWrite, byte[]> serializer, Func<byte[], TRead> deserializer)
        {
            this.serializer = serializer;
            this.deserializer = deserializer;
            this.unaryResponseHandler = HandleUnaryResponse;
            this.finishedHandler = HandleFinished;
            this.writeFinishedHandler = HandleWriteFinished;
            this.readFinishedHandler = HandleReadFinished;
            this.halfclosedHandler = HandleHalfclosed;
            this.finishedServersideHandler = HandleFinishedServerside;
        }

        public void Initialize(Channel channel, CompletionQueueSafeHandle cq, String methodName)
        {
            InitializeInternal(CallSafeHandle.Create(channel.Handle, cq, methodName, channel.Target, Timespec.InfFuture), false);
        }

        public void InitializeServer(CallSafeHandle call)
        {
            InitializeInternal(call, true);
        }

        public TRead UnaryCall(Channel channel, String methodName, TWrite msg)
        {
            using(CompletionQueueSafeHandle cq = CompletionQueueSafeHandle.Create())
            {
                // TODO: handle serialization error...
                byte[] payload = serializer(msg);

                unaryResponseTcs = new TaskCompletionSource<TRead>();

                lock (myLock)
                {
                    Initialize(channel, cq, methodName);
                    started = true;
                    halfcloseRequested = true;
                    readingDone = true;
                }
                call.BlockingUnary(cq, payload, unaryResponseHandler);

                // task should be finished once BlockingUnary returns.
                return unaryResponseTcs.Task.Result;

                // TODO: unwrap aggregate exception...
            }
        }

        public Task<TRead> UnaryCallAsync(TWrite msg)
        {
            lock (myLock)
            {
                started = true;
                halfcloseRequested = true;
                readingDone = true;

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
                readingDone = true;

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

        public Task ServerSideUnaryRequestCallAsync()
        {
            lock (myLock)
            {
                started = true;
                call.StartServerSide(finishedServersideHandler);
                return finishedServersideTcs.Task;
            }
        }

        public Task ServerSideStreamingRequestCallAsync(IObserver<TRead> readObserver)
        {
            lock (myLock)
            {
                started = true;
                call.StartServerSide(finishedServersideHandler);
               
                if (this.readObserver != null)
                {
                    throw new InvalidOperationException("Already registered an observer.");
                }
                this.readObserver = readObserver;
                ReceiveMessageAsync();

                return finishedServersideTcs.Task;
            }
        }

        public Task SendMessageAsync(TWrite msg)
        {
            lock (myLock)
            {
                CheckNotDisposed();
                CheckStarted();
                CheckNoError();

                if (halfcloseRequested)
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
                CheckNotDisposed();
                CheckStarted();
                CheckNoError();

                if (halfcloseRequested)
                {
                    throw new InvalidOperationException("Already halfclosed.");
                }

                call.StartSendCloseFromClient(halfclosedHandler);

                halfcloseRequested = true;
                halfcloseTcs = new TaskCompletionSource<object>();
                return halfcloseTcs.Task;
            }
        }

        public Task SendStatusFromServerAsync(Status status)
        {
            lock (myLock)
            {
                CheckNotDisposed();
                CheckStarted();
                CheckNoError();

                if (halfcloseRequested)
                {
                    throw new InvalidOperationException("Already halfclosed.");
                }

                call.StartSendStatusFromServer(status, halfclosedHandler);
                halfcloseRequested = true;
                halfcloseTcs = new TaskCompletionSource<object>();
                return halfcloseTcs.Task;
            }
        }

        public Task<TRead> ReceiveMessageAsync()
        {
            lock (myLock)
            {
                CheckNotDisposed();
                CheckStarted();
                CheckNoError();

                if (readingDone)
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

        public void Cancel()
        {
            lock (myLock)
            {
                CheckNotDisposed();
                CheckStarted();
                cancelRequested = true;
            }
            // grpc_call_cancel is threadsafe
            call.Cancel();
        }

        public void CancelWithStatus(Status status)
        {
            lock (myLock)
            {
                CheckNotDisposed();
                CheckStarted();
                cancelRequested = true;
            }
            // grpc_call_cancel_with_status is threadsafe
            call.CancelWithStatus(status);
        }

        private void InitializeInternal(CallSafeHandle call, bool server)
        {
            lock (myLock)
            {
                // Make sure this object and the delegated held by it will not be garbage collected
                // before we release this handle.
                gchandle = GCHandle.Alloc(this);
                this.call = call;
                this.server = server;
            }
        }

        private void CheckStarted()
        {
            if (!started)
            {
                throw new InvalidOperationException("Call not started");
            }
        }

        private void CheckNotDisposed()
        {
            if (disposed)
            {
                throw new InvalidOperationException("Call has already been disposed.");
            }
        }

        private void CheckNoError()
        {
            if (errorOccured)
            {
                throw new InvalidOperationException("Error occured when processing call.");
            }
        }

        private bool ReleaseResourcesIfPossible()
        {
            if (!disposed && call != null)
            {
                if (halfclosed && readingDone && finished)
                {
                    ReleaseResources();
                    return true;
                }
            }
            return false;
        }

        private void ReleaseResources()
        {
            if (call != null) {
                call.Dispose();
            }
            gchandle.Free();
            disposed = true;
        }

        private void CompleteStreamObserver(Status status)
        {
            if (status.StatusCode != StatusCode.OK)
            {
                // TODO: wrap to handle exceptions;
                readObserver.OnError(new RpcException(status));
            } else {
                // TODO: wrap to handle exceptions;
                readObserver.OnCompleted();
            }
        }

        /// <summary>
        /// Handler for unary response completion.
        /// </summary>
        private void HandleUnaryResponse(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
                TaskCompletionSource<TRead> tcs;
                lock(myLock)
                {
                    finished = true;
                    halfclosed = true;
                    tcs = unaryResponseTcs;

                    ReleaseResourcesIfPossible();
                }

                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);

                if (error != GRPCOpError.GRPC_OP_OK)
                {
                    tcs.SetException(new RpcException(
                        new Status(StatusCode.Internal, "Internal error occured.")
                    ));
                    return;
                }

                var status = ctx.GetReceivedStatus();
                if (status.StatusCode != StatusCode.OK)
                {
                    tcs.SetException(new RpcException(status));
                    return;
                }

                // TODO: handle deserialize error...
                var msg = deserializer(ctx.GetReceivedMessage());
                tcs.SetResult(msg);
            } 
            catch(Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleWriteFinished(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
                TaskCompletionSource<object> oldTcs = null;
                lock (myLock)
                {
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
            catch(Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleHalfclosed(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
                lock (myLock)
                {
                    halfclosed = true;

                    ReleaseResourcesIfPossible();
                }

                if (error != GRPCOpError.GRPC_OP_OK)
                {
                    halfcloseTcs.SetException(new Exception("Halfclose failed"));

                }
                else
                {
                    halfcloseTcs.SetResult(null);
                }
            }
            catch(Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleReadFinished(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
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
                        readingDone = true;
                    }
                    observer = readObserver;
                    status = finishedStatus;
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
                        ReceiveMessageAsync();
                    }
                    else
                    {
                        if (!server)
                        {
                            if (status.HasValue)
                            {
                                CompleteStreamObserver(status.Value);
                            }
                        } 
                        else 
                        {
                            // TODO: wrap to handle exceptions..
                            observer.OnCompleted();
                        }
                        // TODO: completeStreamObserver serverside...
                    }
               }
            }
            catch(Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleFinished(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);
                var status = ctx.GetReceivedStatus();

                bool wasReadingDone;

                lock (myLock)
                {
                    finished = true;
                    finishedStatus = status;

                    wasReadingDone = readingDone;

                    ReleaseResourcesIfPossible();
                }

                if (wasReadingDone) {
                    CompleteStreamObserver(status);
                }

            }
            catch(Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleFinishedServerside(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);

                lock(myLock)
                {
                    finished = true;

                    // TODO: because of the way server calls are implemented, we need to set
                    // reading done to true here. Should be fixed in the future.
                    readingDone = true;

                    ReleaseResourcesIfPossible();
                }
                // TODO: handle error ...

                finishedServersideTcs.SetResult(null);

            }
            catch(Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }
    }
}