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
