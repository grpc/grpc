#region Copyright notice and license

// Copyright 2014, Google Inc.
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
using System.Diagnostics;
using System.Collections.Concurrent;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// grpc_server from grpc/grpc.h
    /// </summary>
    internal sealed class ServerSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_server_request_call_old")]
        static extern GRPCCallError grpcsharp_server_request_call_old_CALLBACK(ServerSafeHandle server, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("grpc_csharp_ext.dll")]
        static extern ServerSafeHandle grpcsharp_server_create(CompletionQueueSafeHandle cq, IntPtr args);

        // TODO: check int representation size
        [DllImport("grpc_csharp_ext.dll")]
        static extern int grpcsharp_server_add_http2_port(ServerSafeHandle server, string addr);

        // TODO: check int representation size
        [DllImport("grpc_csharp_ext.dll")]
        static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_start(ServerSafeHandle server);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_shutdown(ServerSafeHandle server);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_server_shutdown_and_notify")]
        static extern void grpcsharp_server_shutdown_and_notify_CALLBACK(ServerSafeHandle server, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_server_destroy(IntPtr server);

        private ServerSafeHandle()
        {
        }

        public static ServerSafeHandle NewServer(CompletionQueueSafeHandle cq, IntPtr args)
        {
            // TODO: also grpc_secure_server_create...
            return grpcsharp_server_create(cq, args);
        }

        public int AddPort(string addr)
        {
            // TODO: also grpc_server_add_secure_http2_port...
            return grpcsharp_server_add_http2_port(this, addr);
        }

        public void Start()
        {
            grpcsharp_server_start(this);
        }

        public void Shutdown()
        {
            grpcsharp_server_shutdown(this);
        }

        public void ShutdownAndNotify(EventCallbackDelegate callback)
        {
            grpcsharp_server_shutdown_and_notify_CALLBACK(this, callback);
        }

        public GRPCCallError RequestCall(EventCallbackDelegate callback)
        {
            return grpcsharp_server_request_call_old_CALLBACK(this, callback);
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_server_destroy(handle);
            return true;
        }
    }
}