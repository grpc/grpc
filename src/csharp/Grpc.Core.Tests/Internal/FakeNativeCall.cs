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
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    /// <summary>
    /// For testing purposes.
    /// </summary>
    internal class FakeNativeCall : INativeCall
    {
        private bool shouldStartCallFail;
        public IUnaryResponseClientCallback UnaryResponseClientCallback
        {
            get;
            set;
        }

        public IReceivedStatusOnClientCallback ReceivedStatusOnClientCallback
        {
            get;
            set;
        }

        public IReceivedMessageCallback ReceivedMessageCallback
        {
            get;
            set;
        }

        public IReceivedResponseHeadersCallback ReceivedResponseHeadersCallback
        {
            get;
            set;
        }

        public ISendCompletionCallback SendCompletionCallback
        {
            get;
            set;
        }

        public ISendStatusFromServerCompletionCallback SendStatusFromServerCallback
        {
            get;
            set;
        }

        public IReceivedCloseOnServerCallback ReceivedCloseOnServerCallback
        {
            get;
            set;
        }

        public bool IsCancelled
        {
            get;
            set;
        }

        public bool IsDisposed
        {
            get;
            set;
        }

        public void Cancel()
        {
            IsCancelled = true;
        }

        public void CancelWithStatus(Status status)
        {
            IsCancelled = true;
        }

        public string GetPeer()
        {
            return "PEER";
        }

        public void StartUnary(IUnaryResponseClientCallback callback, SliceBufferSafeHandle payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            StartCallMaybeFail();
            UnaryResponseClientCallback = callback;
        }

        public void StartUnary(BatchContextSafeHandle ctx, SliceBufferSafeHandle payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            StartCallMaybeFail();
            throw new NotImplementedException();
        }

        public void StartClientStreaming(IUnaryResponseClientCallback callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            StartCallMaybeFail();
            UnaryResponseClientCallback = callback;
        }

        public void StartServerStreaming(IReceivedStatusOnClientCallback callback, SliceBufferSafeHandle payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            StartCallMaybeFail();
            ReceivedStatusOnClientCallback = callback;
        }

        public void StartDuplexStreaming(IReceivedStatusOnClientCallback callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            StartCallMaybeFail();
            ReceivedStatusOnClientCallback = callback;
        }

        public void StartReceiveMessage(IReceivedMessageCallback callback)
        {
            ReceivedMessageCallback = callback;
        }

        public void StartReceiveInitialMetadata(IReceivedResponseHeadersCallback callback)
        {
            ReceivedResponseHeadersCallback = callback;
        }

        public void StartSendInitialMetadata(ISendCompletionCallback callback, MetadataArraySafeHandle metadataArray)
        {
            SendCompletionCallback = callback;
        }

        public void StartSendMessage(ISendCompletionCallback callback, SliceBufferSafeHandle payload, WriteFlags writeFlags, bool sendEmptyInitialMetadata)
        {
            SendCompletionCallback = callback;
        }

        public void StartSendCloseFromClient(ISendCompletionCallback callback)
        {
            SendCompletionCallback = callback;
        }

        public void StartSendStatusFromServer(ISendStatusFromServerCompletionCallback callback, Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata,
            SliceBufferSafeHandle payload, WriteFlags writeFlags)
        {
            SendStatusFromServerCallback = callback;
        }

        public void StartServerSide(IReceivedCloseOnServerCallback callback)
        {
            ReceivedCloseOnServerCallback = callback;
        }

        public void Dispose()
        {
            IsDisposed = true;
        }

        /// <summary>
        /// Emulate CallSafeHandle.CheckOk() failure for all future attempts
        /// to start a call.
        /// </summary>
        public void MakeStartCallFail()
        {
            shouldStartCallFail = true;
        }

        private void StartCallMaybeFail()
        {
            if (shouldStartCallFail)
            {
                throw new InvalidOperationException("Start call has failed.");
            }
        }
    }
}
