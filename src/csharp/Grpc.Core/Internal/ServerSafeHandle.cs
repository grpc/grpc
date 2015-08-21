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
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_server from grpc/grpc.h
    /// </summary>
    internal sealed class ServerSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern ServerSafeHandle grpcsharp_server_create(CompletionQueueSafeHandle cq, ChannelArgsSafeHandle args);

        [DllImport("grpc_csharp_ext.dll")]
        static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);

        [DllImport("grpc_csharp_ext.dll")]
        static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_start(ServerSafeHandle server);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_destroy(IntPtr server);

        private ServerSafeHandle()
        {
        }

        public static ServerSafeHandle NewServer(CompletionQueueSafeHandle cq, ChannelArgsSafeHandle args)
        {
            // Increment reference count for the native gRPC environment to make sure we don't do grpc_shutdown() before destroying the server handle.
            // Doing so would make object finalizer crash if we end up abandoning the handle.
            GrpcEnvironment.GrpcNativeInit();
            return grpcsharp_server_create(cq, args);
        }

        public int AddInsecurePort(string addr)
        {
            return grpcsharp_server_add_insecure_http2_port(this, addr);
        }

        public int AddSecurePort(string addr, ServerCredentialsSafeHandle credentials)
        {
            return grpcsharp_server_add_secure_http2_port(this, addr, credentials);
        }

        public void Start()
        {
            grpcsharp_server_start(this);
        }
    
        public void ShutdownAndNotify(BatchCompletionDelegate callback, GrpcEnvironment environment)
        {
            var ctx = BatchContextSafeHandle.Create();
            environment.CompletionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_server_shutdown_and_notify_callback(this, environment.CompletionQueue, ctx);
        }

        public void RequestCall(BatchCompletionDelegate callback, GrpcEnvironment environment)
        {
            var ctx = BatchContextSafeHandle.Create();
            environment.CompletionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_server_request_call(this, environment.CompletionQueue, ctx).CheckOk();
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_server_destroy(handle);
            GrpcEnvironment.GrpcNativeShutdown();
            return true;
        }
            
        // Only to be called after ShutdownAndNotify.
        public void CancelAllCalls()
        {
            grpcsharp_server_cancel_all_calls(this);
        }
    }
}
