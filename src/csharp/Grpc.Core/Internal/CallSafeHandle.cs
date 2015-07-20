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
using System.Diagnostics;
using System.Runtime.InteropServices;
using Grpc.Core;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_call from <grpc/grpc.h>
    /// </summary>
    internal class CallSafeHandle : SafeHandleZeroIsInvalid
    {
        const uint GRPC_WRITE_BUFFER_HINT = 1;
        CompletionRegistry completionRegistry;

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_cancel(CallSafeHandle call);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_unary(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len, MetadataArraySafeHandle metadataArray);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_client_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_server_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len,
            MetadataArraySafeHandle metadataArray);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_send_message(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_send_close_from_client(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_send_status_from_server(CallSafeHandle call, 
            BatchContextSafeHandle ctx, StatusCode statusCode, string statusMessage);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_recv_message(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_serverside(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_call_destroy(IntPtr call);

        private CallSafeHandle()
        {
        }

        public static CallSafeHandle Create(ChannelSafeHandle channel, CompletionRegistry registry, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline)
        {
            var result = grpcsharp_channel_create_call(channel, cq, method, host, deadline);
            result.SetCompletionRegistry(registry);
            return result;
        }

        public void SetCompletionRegistry(CompletionRegistry completionRegistry)
        {
            this.completionRegistry = completionRegistry;
        }

        public void StartUnary(byte[] payload, BatchCompletionDelegate callback, MetadataArraySafeHandle metadataArray)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_start_unary(this, ctx, payload, new UIntPtr((ulong)payload.Length), metadataArray)
                .CheckOk();
        }

        public void StartUnary(byte[] payload, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray)
        {
            grpcsharp_call_start_unary(this, ctx, payload, new UIntPtr((ulong)payload.Length), metadataArray)
                .CheckOk();
        }

        public void StartClientStreaming(BatchCompletionDelegate callback, MetadataArraySafeHandle metadataArray)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_start_client_streaming(this, ctx, metadataArray).CheckOk();
        }

        public void StartServerStreaming(byte[] payload, BatchCompletionDelegate callback, MetadataArraySafeHandle metadataArray)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_start_server_streaming(this, ctx, payload, new UIntPtr((ulong)payload.Length), metadataArray).CheckOk();
        }

        public void StartDuplexStreaming(BatchCompletionDelegate callback, MetadataArraySafeHandle metadataArray)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_start_duplex_streaming(this, ctx, metadataArray).CheckOk();
        }

        public void StartSendMessage(byte[] payload, BatchCompletionDelegate callback)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_send_message(this, ctx, payload, new UIntPtr((ulong)payload.Length)).CheckOk();
        }

        public void StartSendCloseFromClient(BatchCompletionDelegate callback)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_send_close_from_client(this, ctx).CheckOk();
        }

        public void StartSendStatusFromServer(Status status, BatchCompletionDelegate callback)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_send_status_from_server(this, ctx, status.StatusCode, status.Detail).CheckOk();
        }

        public void StartReceiveMessage(BatchCompletionDelegate callback)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_recv_message(this, ctx).CheckOk();
        }

        public void StartServerSide(BatchCompletionDelegate callback)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_call_start_serverside(this, ctx).CheckOk();
        }

        public void Cancel()
        {
            grpcsharp_call_cancel(this).CheckOk();
        }

        public void CancelWithStatus(Status status)
        {
            grpcsharp_call_cancel_with_status(this, status.StatusCode, status.Detail).CheckOk();
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_call_destroy(handle);
            return true;
        }

        private static uint GetFlags(bool buffered)
        {
            return buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
        }
    }
}