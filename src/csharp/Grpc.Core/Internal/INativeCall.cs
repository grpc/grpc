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

namespace Grpc.Core.Internal
{
    internal delegate void UnaryResponseClientHandler(bool success, ClientSideStatus receivedStatus, byte[] receivedMessage, Metadata responseHeaders);

    // Received status for streaming response calls.
    internal delegate void ReceivedStatusOnClientHandler(bool success, ClientSideStatus receivedStatus);

    internal delegate void ReceivedMessageHandler(bool success, byte[] receivedMessage);

    internal delegate void ReceivedResponseHeadersHandler(bool success, Metadata responseHeaders);

    internal delegate void SendCompletionHandler(bool success);

    internal delegate void ReceivedCloseOnServerHandler(bool success, bool cancelled);

    /// <summary>
    /// Abstraction of a native call object.
    /// </summary>
    internal interface INativeCall : IDisposable
    {
        void Cancel();

        void CancelWithStatus(Grpc.Core.Status status);

        string GetPeer();

        void StartUnary(UnaryResponseClientHandler callback, byte[] payload, MetadataArraySafeHandle metadataArray, Grpc.Core.WriteFlags writeFlags);

        void StartUnary(BatchContextSafeHandle ctx, byte[] payload, MetadataArraySafeHandle metadataArray, Grpc.Core.WriteFlags writeFlags);

        void StartClientStreaming(UnaryResponseClientHandler callback, MetadataArraySafeHandle metadataArray);

        void StartServerStreaming(ReceivedStatusOnClientHandler callback, byte[] payload, MetadataArraySafeHandle metadataArray, Grpc.Core.WriteFlags writeFlags);

        void StartDuplexStreaming(ReceivedStatusOnClientHandler callback, MetadataArraySafeHandle metadataArray);

        void StartReceiveMessage(ReceivedMessageHandler callback);

        void StartReceiveInitialMetadata(ReceivedResponseHeadersHandler callback);

        void StartSendInitialMetadata(SendCompletionHandler callback, MetadataArraySafeHandle metadataArray);

        void StartSendMessage(SendCompletionHandler callback, byte[] payload, Grpc.Core.WriteFlags writeFlags, bool sendEmptyInitialMetadata);

        void StartSendCloseFromClient(SendCompletionHandler callback);

        void StartSendStatusFromServer(SendCompletionHandler callback, Grpc.Core.Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata, byte[] optionalPayload, Grpc.Core.WriteFlags writeFlags);

        void StartServerSide(ReceivedCloseOnServerHandler callback);
    }
}
