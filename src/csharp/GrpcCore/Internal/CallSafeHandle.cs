using System;
using System.Runtime.InteropServices;
using System.Diagnostics;
using Google.GRPC.Core;

namespace Google.GRPC.Core.Internal
{
    // TODO: we need to make sure that the delegates are not collected before invoked.
    internal delegate void EventCallbackDelegate(IntPtr eventPtr);

    /// <summary>
    /// grpc_call from <grpc/grpc.h>
    /// </summary>
	internal class CallSafeHandle : SafeHandleZeroIsInvalid
	{
        const UInt32 GRPC_WRITE_BUFFER_HINT = 1;

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_channel_create_call_old(ChannelSafeHandle channel, string method, string host, Timespec deadline);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_add_metadata(CallSafeHandle call, IntPtr metadata, UInt32 flags);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_invoke_old(CallSafeHandle call, CompletionQueueSafeHandle cq, IntPtr metadataReadTag, IntPtr finishedTag, UInt32 flags);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_call_invoke_old")]
        static extern GRPCCallError grpcsharp_call_invoke_old_CALLBACK(CallSafeHandle call, CompletionQueueSafeHandle cq,
                                                              [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate metadataReadCallback, 
                                                              [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate finishedCallback, 
                                                              UInt32 flags);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_server_accept_old(CallSafeHandle call, CompletionQueueSafeHandle completionQueue, IntPtr finishedTag);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_call_server_accept_old")]
        static extern GRPCCallError grpcsharp_call_server_accept_old_CALLBACK(CallSafeHandle call, CompletionQueueSafeHandle completionQueue, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate finishedCallback);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_server_end_initial_metadata_old(CallSafeHandle call, UInt32 flags);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_cancel(CallSafeHandle call);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_write_status_old(CallSafeHandle call, StatusCode statusCode, string statusMessage, IntPtr tag);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_call_start_write_status_old")]
        static extern GRPCCallError grpcsharp_call_start_write_status_old_CALLBACK(CallSafeHandle call, StatusCode statusCode, string statusMessage, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_writes_done_old(CallSafeHandle call, IntPtr tag);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_call_writes_done_old")]
        static extern GRPCCallError grpcsharp_call_writes_done_old_CALLBACK(CallSafeHandle call, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCallError grpcsharp_call_start_read_old(CallSafeHandle call, IntPtr tag);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_call_start_read_old")]
        static extern GRPCCallError grpcsharp_call_start_read_old_CALLBACK(CallSafeHandle call, [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_call_start_write_from_copied_buffer(CallSafeHandle call,
                                                                    byte[] buffer, UIntPtr length,
                                                                    IntPtr tag, UInt32 flags);

        [DllImport("grpc_csharp_ext.dll", EntryPoint = "grpcsharp_call_start_write_from_copied_buffer")]
        static extern void grpcsharp_call_start_write_from_copied_buffer_CALLBACK(CallSafeHandle call,
                                                                             byte[] buffer, UIntPtr length,
                                                                             [MarshalAs(UnmanagedType.FunctionPtr)] EventCallbackDelegate callback,
                                                                             UInt32 flags);

		[DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_call_destroy(IntPtr call);

        private CallSafeHandle()
        {
        }

        /// <summary>
        /// Creates a client call.
        /// </summary>
        public static CallSafeHandle Create(ChannelSafeHandle channel, string method, string host, Timespec deadline)
        {
            return grpcsharp_channel_create_call_old(channel, method, host, deadline);
        }

        public void Invoke(CompletionQueueSafeHandle cq, IntPtr metadataReadTag, IntPtr finishedTag, bool buffered)
        {   
            AssertCallOk(grpcsharp_call_invoke_old(this, cq, metadataReadTag, finishedTag, GetFlags(buffered)));
        }

        public void Invoke(CompletionQueueSafeHandle cq, bool buffered, EventCallbackDelegate metadataReadCallback, EventCallbackDelegate finishedCallback)
        {   
            AssertCallOk(grpcsharp_call_invoke_old_CALLBACK(this, cq, metadataReadCallback, finishedCallback, GetFlags(buffered)));
        }

        public void ServerAccept(CompletionQueueSafeHandle cq, IntPtr finishedTag)
        {
            AssertCallOk(grpcsharp_call_server_accept_old(this, cq, finishedTag));
        }

        public void ServerAccept(CompletionQueueSafeHandle cq, EventCallbackDelegate callback)
        {
            AssertCallOk(grpcsharp_call_server_accept_old_CALLBACK(this, cq, callback));
        }

        public void ServerEndInitialMetadata(UInt32 flags)
        {
            AssertCallOk(grpcsharp_call_server_end_initial_metadata_old(this, flags));
        }

        public void StartWrite(byte[] payload, IntPtr tag, bool buffered)
        {
            grpcsharp_call_start_write_from_copied_buffer(this, payload, new UIntPtr((ulong) payload.Length), tag, GetFlags(buffered));
        }

        public void StartWrite(byte[] payload, bool buffered, EventCallbackDelegate callback)
        {
            grpcsharp_call_start_write_from_copied_buffer_CALLBACK(this, payload, new UIntPtr((ulong) payload.Length), callback, GetFlags(buffered));
        }

        public void StartWriteStatus(Status status, IntPtr tag)
        {
            AssertCallOk(grpcsharp_call_start_write_status_old(this, status.StatusCode, status.Detail, tag));
        }

        public void StartWriteStatus(Status status, EventCallbackDelegate callback)
        {
            AssertCallOk(grpcsharp_call_start_write_status_old_CALLBACK(this, status.StatusCode, status.Detail, callback));
        }

        public void WritesDone(IntPtr tag)
        {
            AssertCallOk(grpcsharp_call_writes_done_old(this, tag));
        }

        public void WritesDone(EventCallbackDelegate callback)
        {
            AssertCallOk(grpcsharp_call_writes_done_old_CALLBACK(this, callback));
        }

        public void StartRead(IntPtr tag)
        {
            AssertCallOk(grpcsharp_call_start_read_old(this, tag));
        }

        public void StartRead(EventCallbackDelegate callback)
        {
            AssertCallOk(grpcsharp_call_start_read_old_CALLBACK(this, callback));
        }

        public void Cancel()
        {
            AssertCallOk(grpcsharp_call_cancel(this));
        }

        public void CancelWithStatus(Status status)
        {
            AssertCallOk(grpcsharp_call_cancel_with_status(this, status.StatusCode, status.Detail));
        }

		protected override bool ReleaseHandle()
		{
            grpcsharp_call_destroy(handle);
			return true;
		}

        private static void AssertCallOk(GRPCCallError callError)
        {
            Trace.Assert(callError == GRPCCallError.GRPC_CALL_OK, "Status not GRPC_CALL_OK");
        }

        private static UInt32 GetFlags(bool buffered) {
            return buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
        }
	}
}