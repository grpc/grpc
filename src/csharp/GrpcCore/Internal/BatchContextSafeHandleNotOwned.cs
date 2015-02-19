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
    /// Not owned version of 
    /// grpcsharp_batch_context
    /// </summary>
    internal class BatchContextSafeHandleNotOwned : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandleNotOwned ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandleNotOwned ctx, byte[] buffer, UIntPtr bufferLen);

        [DllImport("grpc_csharp_ext.dll")]
        static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandleNotOwned ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandleNotOwned ctx);  // returns const char*

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_batch_context_server_rpc_new_call(BatchContextSafeHandleNotOwned ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_method(BatchContextSafeHandleNotOwned ctx);  // returns const char*

        public BatchContextSafeHandleNotOwned(IntPtr handle) : base(false)
        {
            SetHandle(handle);
        }

        public Status GetReceivedStatus()
        {
            // TODO: can the native method return string directly?
            string details = Marshal.PtrToStringAnsi(grpcsharp_batch_context_recv_status_on_client_details(this));
            return new Status(grpcsharp_batch_context_recv_status_on_client_status(this), details);
        }

        public byte[] GetReceivedMessage()
        {
            IntPtr len = grpcsharp_batch_context_recv_message_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int) len];
            grpcsharp_batch_context_recv_message_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        public CallSafeHandle GetServerRpcNewCall() {
            return grpcsharp_batch_context_server_rpc_new_call(this);
        }

        public string GetServerRpcNewMethod() {
            return Marshal.PtrToStringAnsi(grpcsharp_batch_context_server_rpc_new_method(this));
        }
    }
}