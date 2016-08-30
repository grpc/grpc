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
    /// Manages server side native call lifecycle.
    /// </summary>
    internal class AsyncCallServer<TRequest, TResponse> : AsyncCallBase<TResponse, TRequest>
    {
        readonly TaskCompletionSource<object> finishedServersideTcs = new TaskCompletionSource<object>();
        readonly CancellationTokenSource cancellationTokenSource = new CancellationTokenSource();
        readonly Server server;

        public AsyncCallServer(Func<TResponse, byte[]> serializer, Func<byte[], TRequest> deserializer, Server server) : base(serializer, deserializer)
        {
            this.server = GrpcPreconditions.CheckNotNull(server);
        }

        public void Initialize(CallSafeHandle call, CompletionQueueSafeHandle completionQueue)
        {
            call.Initialize(completionQueue);

            server.AddCallReference(this);
            InitializeInternal(call);
        }

        /// <summary>
        /// Only for testing purposes.
        /// </summary>
        public void InitializeForTesting(INativeCall call)
        {
            server.AddCallReference(this);
            InitializeInternal(call);
        }

        /// <summary>
        /// Starts a server side call.
        /// </summary>
        public Task ServerSideCallAsync()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckNotNull(call);

                started = true;

                call.StartServerSide(HandleFinishedServerside);
                return finishedServersideTcs.Task;
            }
        }

        /// <summary>
        /// Sends a streaming response. Only one pending send action is allowed at any given time.
        /// </summary>
        public Task SendMessageAsync(TResponse msg, WriteFlags writeFlags)
        {
            return SendMessageInternalAsync(msg, writeFlags);
        }

        /// <summary>
        /// Receives a streaming request. Only one pending read action is allowed at any given time.
        /// </summary>
        public Task<TRequest> ReadMessageAsync()
        {
            return ReadMessageInternalAsync();
        }

        /// <summary>
        /// Initiates sending a initial metadata. 
        /// Even though C-core allows sending metadata in parallel to sending messages, we will treat sending metadata as a send message operation
        /// to make things simpler.
        /// </summary>
        public Task SendInitialMetadataAsync(Metadata headers)
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckNotNull(headers, "metadata");

                GrpcPreconditions.CheckState(started);
                GrpcPreconditions.CheckState(!initialMetadataSent, "Response headers can only be sent once per call.");
                GrpcPreconditions.CheckState(streamingWritesCounter == 0, "Response headers can only be sent before the first write starts.");

                var earlyResult = CheckSendAllowedOrEarlyResult();
                if (earlyResult != null)
                {
                    return earlyResult;
                }

                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartSendInitialMetadata(HandleSendFinished, metadataArray);
                }

                this.initialMetadataSent = true;
                streamingWriteTcs = new TaskCompletionSource<object>();
                return streamingWriteTcs.Task;
            }
        }

        /// <summary>
        /// Sends call result status, indicating we are done with writes.
        /// Sending a status different from StatusCode.OK will also implicitly cancel the call.
        /// </summary>
        public Task SendStatusFromServerAsync(Status status, Metadata trailers, Tuple<TResponse, WriteFlags> optionalWrite)
        {
            byte[] payload = optionalWrite != null ? UnsafeSerialize(optionalWrite.Item1) : null;
            var writeFlags = optionalWrite != null ? optionalWrite.Item2 : default(WriteFlags);

            lock (myLock)
            {
                GrpcPreconditions.CheckState(started);
                GrpcPreconditions.CheckState(!disposed);
                GrpcPreconditions.CheckState(!halfcloseRequested, "Can only send status from server once.");

                using (var metadataArray = MetadataArraySafeHandle.Create(trailers))
                {
                    call.StartSendStatusFromServer(HandleSendStatusFromServerFinished, status, metadataArray, !initialMetadataSent,
                        payload, writeFlags);
                }
                halfcloseRequested = true;
                initialMetadataSent = true;
                sendStatusFromServerTcs = new TaskCompletionSource<object>();
                if (optionalWrite != null)
                {
                    streamingWritesCounter++;
                }
                return sendStatusFromServerTcs.Task;
            }
        }

        /// <summary>
        /// Gets cancellation token that gets cancelled once close completion
        /// is received and the cancelled flag is set.
        /// </summary>
        public CancellationToken CancellationToken
        {
            get
            {
                return cancellationTokenSource.Token;
            }
        }

        public string Peer
        {
            get
            {
                return call.GetPeer();
            }
        }

        protected override bool IsClient
        {
            get { return false; }
        }

        protected override void OnAfterReleaseResources()
        {
            server.RemoveCallReference(this);
        }

        protected override Task CheckSendAllowedOrEarlyResult()
        {
            GrpcPreconditions.CheckState(!halfcloseRequested, "Response stream has already been completed.");
            GrpcPreconditions.CheckState(!finished, "Already finished.");
            GrpcPreconditions.CheckState(streamingWriteTcs == null, "Only one write can be pending at a time");
            GrpcPreconditions.CheckState(!disposed);

            return null;
        }

        /// <summary>
        /// Handles the server side close completion.
        /// </summary>
        private void HandleFinishedServerside(bool success, bool cancelled)
        {
            // NOTE: because this event is a result of batch containing GRPC_OP_RECV_CLOSE_ON_SERVER,
            // success will be always set to true.
            lock (myLock)
            {
                finished = true;
                if (streamingReadTcs == null)
                {
                    // if there's no pending read, readingDone=true will dispose now.
                    // if there is a pending read, we will dispose once that read finishes.
                    readingDone = true;
                    streamingReadTcs = new TaskCompletionSource<TRequest>();
                    streamingReadTcs.SetResult(default(TRequest));
                }
                ReleaseResourcesIfPossible();
            }

            if (cancelled)
            {
                cancellationTokenSource.Cancel();
            }

            finishedServersideTcs.SetResult(null);
        }
    }
}
