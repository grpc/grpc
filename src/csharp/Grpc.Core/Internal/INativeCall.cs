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
using Grpc.Core;

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

        void CancelWithStatus(Status status);

        string GetPeer();

        void StartUnary(UnaryResponseClientHandler callback, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartUnary(BatchContextSafeHandle ctx, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartClientStreaming(UnaryResponseClientHandler callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartServerStreaming(ReceivedStatusOnClientHandler callback, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartDuplexStreaming(ReceivedStatusOnClientHandler callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartReceiveMessage(ReceivedMessageHandler callback);

        void StartReceiveInitialMetadata(ReceivedResponseHeadersHandler callback);

        void StartSendInitialMetadata(SendCompletionHandler callback, MetadataArraySafeHandle metadataArray);

        void StartSendMessage(SendCompletionHandler callback, byte[] payload, WriteFlags writeFlags, bool sendEmptyInitialMetadata);

        void StartSendCloseFromClient(SendCompletionHandler callback);

        void StartSendStatusFromServer(SendCompletionHandler callback, Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata, byte[] optionalPayload, WriteFlags writeFlags);

        void StartServerSide(ReceivedCloseOnServerHandler callback);
    }
}
