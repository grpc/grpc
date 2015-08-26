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
        [DllImport("grpc_csharp_ext.dll")]
        static extern BatchContextSafeHandle grpcsharp_batch_context_create();

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen);

        [DllImport("grpc_csharp_ext.dll")]
        static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx);  // returns const char*

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_batch_context_server_rpc_new_call(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_method(BatchContextSafeHandle ctx);  // returns const char*

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_host(BatchContextSafeHandle ctx);  // returns const char*

        [DllImport("grpc_csharp_ext.dll")]
        static extern Timespec grpcsharp_batch_context_server_rpc_new_deadline(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_request_metadata(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_batch_context_destroy(IntPtr ctx);

        private BatchContextSafeHandle()
        {
        }

        public static BatchContextSafeHandle Create()
        {
            return grpcsharp_batch_context_create();
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
            IntPtr metadataArrayPtr = grpcsharp_batch_context_recv_initial_metadata(this);
            return MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(metadataArrayPtr);
        }
            
        // Gets data of recv_status_on_client completion.
        public ClientSideStatus GetReceivedStatusOnClient()
        {
            string details = Marshal.PtrToStringAnsi(grpcsharp_batch_context_recv_status_on_client_details(this));
            var status = new Status(grpcsharp_batch_context_recv_status_on_client_status(this), details);

            IntPtr metadataArrayPtr = grpcsharp_batch_context_recv_status_on_client_trailing_metadata(this);
            var metadata = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(metadataArrayPtr);

            return new ClientSideStatus(status, metadata);
        }

        // Gets data of recv_message completion.
        public byte[] GetReceivedMessage()
        {
            IntPtr len = grpcsharp_batch_context_recv_message_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int)len];
            grpcsharp_batch_context_recv_message_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        // Gets data of server_rpc_new completion.
        public ServerRpcNew GetServerRpcNew(Server server)
        {
            var call = grpcsharp_batch_context_server_rpc_new_call(this);

            var method = Marshal.PtrToStringAnsi(grpcsharp_batch_context_server_rpc_new_method(this));
            var host = Marshal.PtrToStringAnsi(grpcsharp_batch_context_server_rpc_new_host(this));
            var deadline = grpcsharp_batch_context_server_rpc_new_deadline(this);

            IntPtr metadataArrayPtr = grpcsharp_batch_context_server_rpc_new_request_metadata(this);
            var metadata = MetadataArraySafeHandle.ReadMetadataFromPtrUnsafe(metadataArrayPtr);

            return new ServerRpcNew(server, call, method, host, deadline, metadata);
        }

        // Gets data of receive_close_on_server completion.
        public bool GetReceivedCloseOnServerCancelled()
        {
            return grpcsharp_batch_context_recv_close_on_server_cancelled(this) != 0;
        }
            
        protected override bool ReleaseHandle()
        {
            grpcsharp_batch_context_destroy(handle);
            return true;
        }
    }

    /// <summary>
    /// Status + metadata received on client side when call finishes.
    /// (when receive_status_on_client operation finishes).
    /// </summary>
    internal struct ClientSideStatus
    {
        readonly Status status;
        readonly Metadata trailers;

        public ClientSideStatus(Status status, Metadata trailers)
        {
            this.status = status;
            this.trailers = trailers;
        }

        public Status Status
        {
            get
            {
                return this.status;
            }    
        }

        public Metadata Trailers
        {
            get
            {
                return this.trailers;
            }
        }
    }

    /// <summary>
    /// Details of a newly received RPC.
    /// </summary>
    internal struct ServerRpcNew
    {
        readonly Server server;
        readonly CallSafeHandle call;
        readonly string method;
        readonly string host;
        readonly Timespec deadline;
        readonly Metadata requestMetadata;

        public ServerRpcNew(Server server, CallSafeHandle call, string method, string host, Timespec deadline, Metadata requestMetadata)
        {
            this.server = server;
            this.call = call;
            this.method = method;
            this.host = host;
            this.deadline = deadline;
            this.requestMetadata = requestMetadata;
        }

        public Server Server
        {
            get
            {
                return this.server;
            }
        }

        public CallSafeHandle Call
        {
            get
            {
                return this.call;
            }
        }

        public string Method
        {
            get
            {
                return this.method;
            }
        }

        public string Host
        {
            get
            {
                return this.host;
            }
        }

        public Timespec Deadline
        {
            get
            {
                return this.deadline;
            }
        }

        public Metadata RequestMetadata
        {
            get
            {
                return this.requestMetadata;
            }
        }
    }
}