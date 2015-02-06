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
        [DllImport("libgrpc.so", EntryPoint = "grpc_server_request_call_old")]
        static extern GRPCCallError grpc_server_request_call_old_CALLBACK(ServerSafeHandle server, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("libgrpc.so")]
        static extern ServerSafeHandle grpc_server_create(CompletionQueueSafeHandle cq, IntPtr args);

        // TODO: check int representation size
        [DllImport("libgrpc.so")]
        static extern int grpc_server_add_http2_port(ServerSafeHandle server, string addr);

        // TODO: check int representation size
        [DllImport("libgrpc.so")]
        static extern int grpc_server_add_secure_http2_port(ServerSafeHandle server, string addr);

        [DllImport("libgrpc.so")]
        static extern void grpc_server_start(ServerSafeHandle server);

        [DllImport("libgrpc.so")]
        static extern void grpc_server_shutdown(ServerSafeHandle server);

        [DllImport("libgrpc.so", EntryPoint = "grpc_server_shutdown_and_notify")]
        static extern void grpc_server_shutdown_and_notify_CALLBACK(ServerSafeHandle server, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("libgrpc.so")]
        static extern void grpc_server_destroy(IntPtr server);

        private ServerSafeHandle()
        {
        }

        public static ServerSafeHandle NewServer(CompletionQueueSafeHandle cq, IntPtr args)
        {
            // TODO: also grpc_secure_server_create...
            return grpc_server_create(cq, args);
        }

        public int AddPort(string addr)
        {
            // TODO: also grpc_server_add_secure_http2_port...
            return grpc_server_add_http2_port(this, addr);
        }

        public void Start()
        {
            grpc_server_start(this);
        }

        public void Shutdown()
        {
            grpc_server_shutdown(this);
        }

        public void ShutdownAndNotify(EventCallbackDelegate callback)
        {
            grpc_server_shutdown_and_notify_CALLBACK(this, callback);
        }

        public GRPCCallError RequestCall(EventCallbackDelegate callback)
        {
            return grpc_server_request_call_old_CALLBACK(this, callback);
        }

        protected override bool ReleaseHandle()
        {
            grpc_server_destroy(handle);
            return true;
        }
    }
}