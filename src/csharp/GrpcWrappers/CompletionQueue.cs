using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
	public class CompletionQueue : WrappedNativeObject<CompletionQueueSafeHandle>
	{
		[DllImport("libgrpc.so")]
		static extern CompletionQueueSafeHandle grpc_completion_queue_create();

		[DllImport("libgrpc.so")]
		static extern EventSafeHandle grpc_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag, Timespec deadline);

		public CompletionQueue() : base(grpc_completion_queue_create())
		{
		}

		public Event Next(Timespec deadline)
		{
			return handle.Next(deadline);
		}

		public Event Pluck(IntPtr tag, Timespec deadline)
		{
			using (EventSafeHandle eventHandle = grpc_completion_queue_pluck(handle, tag, deadline))
			{
				if (eventHandle.IsInvalid)
				{
					return null;
				}
				return new Event(eventHandle);
			}
		}
	}

	public class CompletionQueueSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc.so")]
		static extern EventSafeHandle grpc_completion_queue_next(IntPtr cq, Timespec deadline);

		[DllImport("libgrpc.so")]
		static extern void grpc_completion_queue_shutdown(IntPtr cq);

		[DllImport("libgrpc.so")]
		static extern void grpc_completion_queue_destroy(IntPtr cq);

		protected override bool ReleaseHandle()
		{
			// TODO: not sure if shutting down in ReleaseHandle is fine.
			grpc_completion_queue_shutdown(handle);
			while (Next(Timespec.InfFuture).CompletionType != GRPCCompletionType.GRPC_QUEUE_SHUTDOWN)
			{
			}

			grpc_completion_queue_destroy(handle);
			return true;
		}

		public Event Next(Timespec deadline)
		{
			using (EventSafeHandle eventHandle = grpc_completion_queue_next(handle, deadline))
			{
				if (eventHandle.IsInvalid)
				{
					return null;
				}
				return new Event(eventHandle);
			}
		}
	}
}

