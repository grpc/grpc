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