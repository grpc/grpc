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
using System.Runtime.InteropServices;
using Grpc.Core;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpcsharp_batch_context
    /// </summary>
    internal class BatchContextSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private BatchContextSafeHandle()
        {
        }

        public static BatchContextSafeHandle Create()
        {
            return Native.grpcsharp_batch_context_create();
        }

        public IntPtr Handle
        {
            get
            {
                return handle;
            }
        }

        // Gets data of recv_initial_metadata completion.
        public unsafe Metadata GetReceivedInitialMetadata()
        {
            var batchContext = (BatchContext*) handle;
            return MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(&batchContext->recvInitialMetadata);
        }
            
        // Gets data of recv_status_on_client completion.
        public unsafe ClientSideStatus GetReceivedStatusOnClient()
        {
            var batchContext = (BatchContext*) handle;
            string details = Marshal.PtrToStringAnsi(batchContext->recvStatusOnClient.statusDetails);
            var status = new Status((StatusCode) batchContext->recvStatusOnClient.status, details);
            var metadata = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(&batchContext->recvStatusOnClient.trailingMetadata);
            return new ClientSideStatus(status, metadata);
        }

        // Gets data of recv_message completion.
        public byte[] GetReceivedMessage()
        {
            // TODO(jtattermusch): implement using an unsafe block to save transitions.
            IntPtr len = Native.grpcsharp_batch_context_recv_message_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int)len];
            Native.grpcsharp_batch_context_recv_message_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        // Gets data of server_rpc_new completion.
        public unsafe ServerRpcNew GetServerRpcNew(Server server)
        {
            var batchContext = (BatchContext*) handle;
            var serverRpcNew = &(batchContext->serverRpcNew);

            var call = new CallSafeHandle(serverRpcNew->call);
            var method = Marshal.PtrToStringAnsi(serverRpcNew->callDetails.method);
            var host = Marshal.PtrToStringAnsi(serverRpcNew->callDetails.host);
            var deadline = serverRpcNew->callDetails.deadline;
            var metadata = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(&serverRpcNew->requestMetadata);
            return new ServerRpcNew(server, call, method, host, deadline, metadata);
        }

        // Gets data of receive_close_on_server completion.
        public unsafe bool GetReceivedCloseOnServerCancelled()
        {
            var batchContext = (BatchContext*) handle;
            return (batchContext->recvCloseOnServerCancelled != 0);
        }
            
        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_batch_context_destroy(handle);
            return true;
        }

        /// <summary>
        /// Corresponds to grpcsharp_batch_context.
        /// IMPORTANT: The struct layout needs to be kept in sync.
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        internal struct BatchContext
        {
            MetadataArraySafeHandle.MetadataArray sendInitialMetadata;
            IntPtr sendMessage;
            SendStatusFromServer sendStatusFromServer;
            public MetadataArraySafeHandle.MetadataArray recvInitialMetadata;
            public IntPtr recvMessage;
            public RecvStatusOnClient recvStatusOnClient;
            public int recvCloseOnServerCancelled;
            public ServerRpcNewNative serverRpcNew;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct SendStatusFromServer
        {
            public MetadataArraySafeHandle.MetadataArray trailingMetadata;
            public IntPtr statusDetails;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct RecvStatusOnClient
        {
            public MetadataArraySafeHandle.MetadataArray trailingMetadata;
            public int status;
            public IntPtr statusDetails;
            public UIntPtr statusDetailsCapacity;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ServerRpcNewNative
        {
            public IntPtr call;
            public CallDetails callDetails;
            public MetadataArraySafeHandle.MetadataArray requestMetadata;
        }

        // corresponds to grpc_call_details
        [StructLayout(LayoutKind.Sequential)]
        internal struct CallDetails
        {
            public IntPtr method;
            public UIntPtr methodCapacity;
            public IntPtr host;
            public UIntPtr hostCapacity;
            public Timespec deadline;
            public uint flags;
            public IntPtr reserved;
        }
    }
}
