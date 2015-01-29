using System;
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// Wrapper to work with native grpc_call.
    /// </summary>
	internal class Call : WrappedNativeObject<CallSafeHandle>
	{
		const UInt32 GRPC_WRITE_BUFFER_HINT = 1;

		[DllImport("libgrpc.so")]
		static extern CallSafeHandle grpc_channel_create_call(ChannelSafeHandle channel, string method, string host, Timespec deadline);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_add_metadata(CallSafeHandle call, IntPtr metadata, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_invoke(CallSafeHandle call, CompletionQueueSafeHandle completionQueue, IntPtr metadataReadTag, IntPtr finishedTag, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_accept(CallSafeHandle call, CompletionQueueSafeHandle completionQueue, IntPtr finishedTag, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_server_accept(CallSafeHandle call, CompletionQueueSafeHandle completionQueue, IntPtr finishedTag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_server_end_initial_metadata(CallSafeHandle call, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_cancel(CallSafeHandle call);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_write_status(CallSafeHandle call, StatusCode statusCode, string statusMessage, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_writes_done(CallSafeHandle call, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_read(CallSafeHandle call, IntPtr tag);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpc_call_start_write_from_copied_buffer(CallSafeHandle call,
                                                            byte[] buffer, UIntPtr length,
                                                            IntPtr tag, UInt32 flags);


		public Call(Channel channel, string method, string host, Timespec deadline)
			: base(grpc_channel_create_call(channel.Handle, method, host, deadline))
		{
		}

		public void Invoke(CompletionQueue cq, IntPtr metadataReadTag, IntPtr finishedTag, bool buffered)
		{	
			AssertCallOk(grpc_call_invoke(handle, cq.Handle, metadataReadTag, finishedTag, GetFlags(buffered)));
		}

		public void StartWrite(byte[] payload, IntPtr tag, bool buffered)
		{
			grpc_call_start_write_from_copied_buffer(handle, payload, new UIntPtr((ulong) payload.Length), tag, GetFlags(buffered));
		}

		public void WritesDone(IntPtr tag)
		{
            AssertCallOk(grpc_call_writes_done(handle, tag));
		}

		public void StartRead(IntPtr tag)
		{
			AssertCallOk(grpc_call_start_read(handle, tag));
		}

		public void Cancel()
		{
            AssertCallOk(grpc_call_cancel(handle));
		}

        public void CancelWithStatus(Status status)
        {
            AssertCallOk(grpc_call_cancel_with_status(handle, status.StatusCode, status.Detail));
        }

        private static void AssertCallOk(GRPCCallError callError)
        {
            Trace.Assert(callError == GRPCCallError.GRPC_CALL_OK, "Status not GRPC_CALL_OK");
        }

        private static UInt32 GetFlags(bool buffered) {
            return buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
        }
	}

	internal class CallSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc.so")]
		static extern void grpc_call_destroy(IntPtr call);

		protected override bool ReleaseHandle()
		{
			grpc_call_destroy(handle);
			return true;
		}
	}
}