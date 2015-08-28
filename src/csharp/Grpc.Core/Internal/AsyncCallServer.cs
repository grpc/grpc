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

        public AsyncCallServer(Func<TResponse, byte[]> serializer, Func<byte[], TRequest> deserializer, GrpcEnvironment environment, Server server) : base(serializer, deserializer, environment)
        {
            this.server = Preconditions.CheckNotNull(server);
        }

        public void Initialize(CallSafeHandle call)
        {
            call.SetCompletionRegistry(environment.CompletionRegistry);

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
                Preconditions.CheckNotNull(call);

                started = true;

                call.StartServerSide(HandleFinishedServerside);
                return finishedServersideTcs.Task;
            }
        }

        /// <summary>
        /// Sends a streaming response. Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendMessage(TResponse msg, WriteFlags writeFlags, AsyncCompletionDelegate<object> completionDelegate)
        {
            StartSendMessageInternal(msg, writeFlags, completionDelegate);
        }

        /// <summary>
        /// Receives a streaming request. Only one pending read action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartReadMessage(AsyncCompletionDelegate<TRequest> completionDelegate)
        {
            StartReadMessageInternal(completionDelegate);
        }

        /// <summary>
        /// Initiates sending a initial metadata. 
        /// Even though C-core allows sending metadata in parallel to sending messages, we will treat sending metadata as a send message operation
        /// to make things simpler.
        /// completionDelegate is invoked upon completion.
        /// </summary>
        public void StartSendInitialMetadata(Metadata headers, AsyncCompletionDelegate<object> completionDelegate)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(headers, "metadata");
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");

                Preconditions.CheckState(!initialMetadataSent, "Response headers can only be sent once per call.");
                Preconditions.CheckState(streamingWritesCounter == 0, "Response headers can only be sent before the first write starts.");
                CheckSendingAllowed();

                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");

                using (var metadataArray = MetadataArraySafeHandle.Create(headers))
                {
                    call.StartSendInitialMetadata(HandleSendFinished, metadataArray);
                }

                this.initialMetadataSent = true;
                sendCompletionDelegate = completionDelegate;
            }
        }

        /// <summary>
        /// Sends call result status, also indicating server is done with streaming responses.
        /// Only one pending send action is allowed at any given time.
        /// completionDelegate is called when the operation finishes.
        /// </summary>
        public void StartSendStatusFromServer(Status status, Metadata trailers, AsyncCompletionDelegate<object> completionDelegate)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(completionDelegate, "Completion delegate cannot be null");
                CheckSendingAllowed();

                using (var metadataArray = MetadataArraySafeHandle.Create(trailers))
                {
                    call.StartSendStatusFromServer(HandleHalfclosed, status, metadataArray, !initialMetadataSent);
                }
                halfcloseRequested = true;
                readingDone = true;
                sendCompletionDelegate = completionDelegate;
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

        protected override void CheckReadingAllowed()
        {
            base.CheckReadingAllowed();
            Preconditions.CheckArgument(!cancelRequested);
        }

        protected override void OnAfterReleaseResources()
        {
            server.RemoveCallReference(this);
        }

        /// <summary>
        /// Handles the server side close completion.
        /// </summary>
        private void HandleFinishedServerside(bool success, bool cancelled)
        {
            lock (myLock)
            {
                finished = true;

                if (cancelled)
                {
                    // Once we cancel, we don't have to care that much 
                    // about reads and writes.

                    // TODO(jtattermusch): is this still necessary?
                    Cancel();
                }

                ReleaseResourcesIfPossible();
            }
            // TODO(jtattermusch): handle error

            if (cancelled)
            {
                cancellationTokenSource.Cancel();
            }

            finishedServersideTcs.SetResult(null);
        }
    }
}