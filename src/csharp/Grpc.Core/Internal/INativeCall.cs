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
    internal interface IUnaryResponseClientCallback
    {
        void OnUnaryResponseClient(bool success, ClientSideStatus receivedStatus, byte[] receivedMessage, Metadata responseHeaders);
    }

    // Received status for streaming response calls.
    internal interface IReceivedStatusOnClientCallback
    {
        void OnReceivedStatusOnClient(bool success, ClientSideStatus receivedStatus);
    }

    internal interface IReceivedMessageCallback
    {
        void OnReceivedMessage(bool success, byte[] receivedMessage);
    }

    internal interface IReceivedResponseHeadersCallback
    {
        void OnReceivedResponseHeaders(bool success, Metadata responseHeaders);
    }

    internal interface ISendCompletionCallback
    {
        void OnSendCompletion(bool success);
    }

    internal interface ISendStatusFromServerCompletionCallback
    {
        void OnSendStatusFromServerCompletion(bool success);
    }

    internal interface IReceivedCloseOnServerCallback
    {
        void OnReceivedCloseOnServer(bool success, bool cancelled);
    }

    /// <summary>
    /// Abstraction of a native call object.
    /// </summary>
    internal interface INativeCall : IDisposable
    {
        void Cancel();

        void CancelWithStatus(Status status);

        string GetPeer();

        void StartUnary(IUnaryResponseClientCallback callback, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartUnary(BatchContextSafeHandle ctx, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartClientStreaming(IUnaryResponseClientCallback callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartServerStreaming(IReceivedStatusOnClientCallback callback, byte[] payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartDuplexStreaming(IReceivedStatusOnClientCallback callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags);

        void StartReceiveMessage(IReceivedMessageCallback callback);

        void StartReceiveInitialMetadata(IReceivedResponseHeadersCallback callback);

        void StartSendInitialMetadata(ISendCompletionCallback callback, MetadataArraySafeHandle metadataArray);

        void StartSendMessage(ISendCompletionCallback callback, byte[] payload, WriteFlags writeFlags, bool sendEmptyInitialMetadata);

        void StartSendCloseFromClient(ISendCompletionCallback callback);

        void StartSendStatusFromServer(ISendStatusFromServerCompletionCallback callback, Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata, byte[] optionalPayload, WriteFlags writeFlags);

        void StartServerSide(IReceivedCloseOnServerCallback callback);
    }
}
