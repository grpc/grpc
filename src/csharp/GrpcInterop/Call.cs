using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
	public class Call : WrappedNative
	{
		const UInt32 GRPC_WRITE_BUFFER_HINT = 1;

		// returns grpc_call*
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_channel_create_call(IntPtr channel, string method, string host, Timespec deadline);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_add_metadata(IntPtr call, IntPtr metadata, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_invoke(IntPtr call, IntPtr completionQueue, IntPtr invokeAcceptedTag, IntPtr metadataReadTag, IntPtr finishedTag, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_accept(IntPtr call, IntPtr completionQueue, IntPtr finishedTag, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_server_accept(IntPtr call, IntPtr completionQueue, IntPtr finishedTag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_server_end_initial_metadata(IntPtr call, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_cancel(IntPtr call);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_cancel_with_status(IntPtr call, StatusCode status, string description);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_write(IntPtr call, IntPtr byteBuffer, IntPtr tag, UInt32 flags);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_write_status(IntPtr call, StatusCode statusCode, string statusMessage, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_writes_done(IntPtr call, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern GRPCCallError grpc_call_start_read(IntPtr call, IntPtr tag);

		[DllImport("libgrpc.so")]
		static extern void grpc_call_destroy(IntPtr call);

		readonly Channel channel;
		readonly string method;
		readonly string host;
		readonly Timespec deadline;

		// TODO: refer to this.method, this.host, this.deadline, otherwise it might be unsafe...
		public Call(Channel channel, string method, string host, Timespec deadline)
			: base(() => grpc_channel_create_call(channel.RawPointer, method, host, deadline))
		{
			this.channel = channel;
			this.method = method;
			this.host = host;
			this.deadline = deadline;
		}

		public GRPCCallError StartInvoke(CompletionQueue cq, IntPtr invokeAcceptedTag, IntPtr metadataReadTag, IntPtr finishedTag, bool buffered)
		{
			UInt32 flags = buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
			return grpc_call_start_invoke(RawPointer, cq.RawPointer, invokeAcceptedTag, metadataReadTag, finishedTag, flags);
		}

		public GRPCCallError StartWrite(ByteBuffer bb, IntPtr tag, bool buffered)
		{
			UInt32 flags = buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
			return grpc_call_start_write(RawPointer, bb.RawPointer, tag, flags);
		}

		public GRPCCallError WritesDone(IntPtr tag)
		{
			return grpc_call_writes_done(RawPointer, tag);
		}

		public GRPCCallError StartRead(IntPtr tag)
		{
			return grpc_call_start_read(RawPointer, tag);
		}

		public GRPCCallError Cancel()
		{
			return grpc_call_cancel(RawPointer);
		}

		protected override void Destroy()
		{
			grpc_call_destroy(RawPointer);
		}
	}
}