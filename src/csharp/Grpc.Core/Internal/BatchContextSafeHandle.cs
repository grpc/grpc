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
using System.Runtime.InteropServices;
using System.Text;
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
        public Metadata GetReceivedInitialMetadata()
        {
            IntPtr metadataArrayPtr = Native.grpcsharp_batch_context_recv_initial_metadata(this);
            return MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(metadataArrayPtr);
        }
            
        // Gets data of recv_status_on_client completion.
        public ClientSideStatus GetReceivedStatusOnClient()
        {
            UIntPtr detailsLength;
            IntPtr detailsPtr = Native.grpcsharp_batch_context_recv_status_on_client_details(this, out detailsLength);
            string details = MarshalUtils.PtrToStringUTF8(detailsPtr, (int) detailsLength.ToUInt32());
            var status = new Status(Native.grpcsharp_batch_context_recv_status_on_client_status(this), details);

            IntPtr metadataArrayPtr = Native.grpcsharp_batch_context_recv_status_on_client_trailing_metadata(this);
            var metadata = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(metadataArrayPtr);

            return new ClientSideStatus(status, metadata);
        }

        // Gets data of recv_message completion.
        public byte[] GetReceivedMessage()
        {
            IntPtr len = Native.grpcsharp_batch_context_recv_message_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int)len];
            Native.grpcsharp_batch_context_recv_message_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        // Gets data of receive_close_on_server completion.
        public bool GetReceivedCloseOnServerCancelled()
        {
            return Native.grpcsharp_batch_context_recv_close_on_server_cancelled(this) != 0;
        }
            
        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_batch_context_destroy(handle);
            return true;
        }
    }
}
