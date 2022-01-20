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
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Manages server side native call lifecycle.
    /// </summary>
    internal class AsyncCallServer<TRequest, TResponse> : AsyncCallBase<TResponse, TRequest>, IReceivedCloseOnServerCallback, ISendStatusFromServerCompletionCallback
    {
        readonly TaskCompletionSource<object> finishedServersideTcs = new TaskCompletionSource<object>();
        readonly CancellationTokenSource cancellationTokenSource = new CancellationTokenSource();
        readonly Server server;

        public AsyncCallServer(Action<TResponse, SerializationContext> serializer, Func<DeserializationContext, TRequest> deserializer, Server server) : base(serializer, deserializer)
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

                Started = true;

                call.StartServerSide(ReceiveCloseOnServerCallback);
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

                GrpcPreconditions.CheckState(Started);
                GrpcPreconditions.CheckState(!InitialMetadataSent, "Response headers can only be sent once per call.");
                GrpcPreconditions.CheckState(streamingWritesCounter == 0, "Response headers can only be sent before the first write starts.");

                var earlyResult = CheckSendAllowedOrEarlyResult();
                if (earlyResult != null)
                {
                    return earlyResult;
                }

                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartSendInitialMetadata(SendCompletionCallback, metadataArray);
                }

                this.InitialMetadataSent = true;
                return InitializeStreamingWrite();
            }
        }

        /// <summary>
        /// Sends call result status, indicating we are done with writes.
        /// Sending a status different from StatusCode.OK will also implicitly cancel the call.
        /// </summary>
        public Task SendStatusFromServerAsync(Status status, Metadata trailers, ResponseWithFlags? optionalWrite)
        {
            using (var serializationScope = DefaultSerializationContext.GetInitializedThreadLocalScope())
            {
                var payload = optionalWrite.HasValue ? UnsafeSerialize(optionalWrite.Value.Response, serializationScope.Context) : SliceBufferSafeHandle.NullInstance;
                var writeFlags = optionalWrite.HasValue ? optionalWrite.Value.WriteFlags : default(WriteFlags);

                lock (myLock)
                {
                    GrpcPreconditions.CheckState(Started);
                    GrpcPreconditions.CheckState(!Disposed);
                    GrpcPreconditions.CheckState(!HalfCloseRequested, "Can only send status from server once.");

                    using (var metadataArray = MetadataArraySafeHandle.Create(trailers))
                    {
                        call.StartSendStatusFromServer(SendStatusFromServerCompletionCallback, status, metadataArray, !InitialMetadataSent,
                            payload, writeFlags);
                    }
                    HalfCloseRequested = true;
                    InitialMetadataSent = true;
                    var result = InitializeSendStatusFromServer();
                    if (optionalWrite.HasValue)
                    {
                        streamingWritesCounter++;
                    }
                    return result;
                }
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

        protected override Exception GetRpcExceptionClientOnly()
        {
            throw new InvalidOperationException("Call be only called for client calls");
        }

        protected override void OnAfterReleaseResourcesLocked()
        {
            server.RemoveCallReference(this);
        }

        protected override Task CheckSendAllowedOrEarlyResult()
        {
            GrpcPreconditions.CheckState(!HalfCloseRequested, "Response stream has already been completed.");
            GrpcPreconditions.CheckState(!Finished, "Already finished.");
            GrpcPreconditions.CheckState(!StreamingWriteInitialized, "Only one write can be pending at a time");
            GrpcPreconditions.CheckState(!Disposed);

            return null;
        }

        /// <summary>
        /// Handles the server side close completion.
        /// </summary>
        private void HandleFinishedServerside(bool success, bool cancelled)
        {
            // NOTE: because this event is a result of batch containing GRPC_OP_RECV_CLOSE_ON_SERVER,
            // success will be always set to true.
            bool releasedResources;
            lock (myLock)
            {
                Finished = true;
                if (!StreamingReadInitialized)
                {
                    // if there's no pending read, readingDone=true will dispose now.
                    // if there is a pending read, we will dispose once that read finishes.
                    ReadingDone = true;
                    InitializeStreamingRead(default(TRequest));
                }
                releasedResources = ReleaseResourcesIfPossible();
            }

            if (releasedResources)
            {
                OnAfterReleaseResourcesUnlocked();
            }

            if (cancelled)
            {
                cancellationTokenSource.Cancel();
            }

            finishedServersideTcs.SetResult(null);
        }

        IReceivedCloseOnServerCallback ReceiveCloseOnServerCallback => this;

        void IReceivedCloseOnServerCallback.OnReceivedCloseOnServer(bool success, bool cancelled)
        {
            HandleFinishedServerside(success, cancelled);
        }

        ISendStatusFromServerCompletionCallback SendStatusFromServerCompletionCallback => this;

        void ISendStatusFromServerCompletionCallback.OnSendStatusFromServerCompletion(bool success)
        {
            HandleSendStatusFromServerFinished(success);
        }

        public struct ResponseWithFlags
        {
            public ResponseWithFlags(TResponse response, WriteFlags writeFlags)
            {
                this.Response = response;
                this.WriteFlags = writeFlags;
            }

            public TResponse Response { get; }
            public WriteFlags WriteFlags { get; }
        }
    }
}
