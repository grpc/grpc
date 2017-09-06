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
        public UnaryResponseClientHandler UnaryResponseClientHandler
        {
            get;
            set;
        }

        public ReceivedStatusOnClientHandler ReceivedStatusOnClientHandler
        {
            get;
            set;
        }

        public ReceivedMessageHandler ReceivedMessageHandler
        {
            get;
            set;
        }

        public ReceivedResponseHeadersHandler ReceivedResponseHeadersHandler
        {
            get;
            set;
        }

        public SendCompletionHandler SendCompletionHandler
        {
            get;
            set;
        }

        public SendCompletionHandler SendStatusFromServerHandler
        {
            get;
            set;
        }

        public ReceivedCloseOnServerHandler ReceivedCloseOnServerHandler
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

        public void StartUnary(UnaryResponseClientHandler callback, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            UnaryResponseClientHandler = callback;
        }

        public void StartUnary(BatchContextSafeHandle ctx, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            throw new NotImplementedException();
        }

        public void StartClientStreaming(UnaryResponseClientHandler callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            UnaryResponseClientHandler = callback;
        }

        public void StartServerStreaming(ReceivedStatusOnClientHandler callback, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            ReceivedStatusOnClientHandler = callback;
        }

        public void StartDuplexStreaming(ReceivedStatusOnClientHandler callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            ReceivedStatusOnClientHandler = callback;
        }

        public void StartReceiveMessage(ReceivedMessageHandler callback)
        {
            ReceivedMessageHandler = callback;
        }

        public void StartReceiveInitialMetadata(ReceivedResponseHeadersHandler callback)
        {
            ReceivedResponseHeadersHandler = callback;
        }

        public void StartSendInitialMetadata(SendCompletionHandler callback, MetadataArraySafeHandle metadataArray)
        {
            SendCompletionHandler = callback;
        }

        public void StartSendMessage(SendCompletionHandler callback, byte[] payload, WriteFlags writeFlags, bool sendEmptyInitialMetadata)
        {
            SendCompletionHandler = callback;
        }

        public void StartSendCloseFromClient(SendCompletionHandler callback)
        {
            SendCompletionHandler = callback;
        }

        public void StartSendStatusFromServer(SendCompletionHandler callback, Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata,
            byte[] optionalPayload, WriteFlags writeFlags)
        {
            SendStatusFromServerHandler = callback;
        }

        public void StartServerSide(ReceivedCloseOnServerHandler callback)
        {
            ReceivedCloseOnServerHandler = callback;
        }

        public void Dispose()
        {
            IsDisposed = true;
        }
    }
}
