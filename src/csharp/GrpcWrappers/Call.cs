using System;
using System.Runtime.InteropServices;

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
		static extern GRPCCallError grpc_call_start_write(CallSafeHandle call, ByteBufferSafeHandle byteBuffer, IntPtr tag, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_write_status(CallSafeHandle call, StatusCode statusCode, string statusMessage, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_writes_done(CallSafeHandle call, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_read(CallSafeHandle call, IntPtr tag);

		public Call(Channel channel, string method, string host, Timespec deadline)
			: base(grpc_channel_create_call(channel.Handle, method, host, deadline))
		{
		}

		public GRPCCallError Invoke(CompletionQueue cq, IntPtr metadataReadTag, IntPtr finishedTag, bool buffered)
		{
			UInt32 flags = buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
			return grpc_call_invoke(handle, cq.Handle, metadataReadTag, finishedTag, flags);
		}

		public GRPCCallError StartWrite(ByteBuffer bb, IntPtr tag, bool buffered)
		{
			UInt32 flags = buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
			return grpc_call_start_write(handle, bb.Handle, tag, flags);
		}

		public GRPCCallError WritesDone(IntPtr tag)
		{
			return grpc_call_writes_done(handle, tag);
		}

		public GRPCCallError StartRead(IntPtr tag)
		{
			return grpc_call_start_read(handle, tag);
		}

		public GRPCCallError Cancel()
		{
			return grpc_call_cancel(handle);
		}

        public GRPCCallError CancelWithStatus(Status status)
        {
            return grpc_call_cancel_with_status(handle, status.StatusCode, status.Detail);
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