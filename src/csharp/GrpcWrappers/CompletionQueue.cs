using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
	internal class CompletionQueue : WrappedNativeObject<CompletionQueueSafeHandle>
	{
		[DllImport("libgrpc.so")]
		static extern CompletionQueueSafeHandle grpc_completion_queue_create();

		[DllImport("libgrpc.so")]
		static extern EventSafeHandle grpc_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag, Timespec deadline);

        [DllImport("libgrpc.so")]
        static extern EventSafeHandle grpc_completion_queue_next(CompletionQueueSafeHandle cq, Timespec deadline);


		public CompletionQueue() : base(grpc_completion_queue_create())
		{
		}

		public EventSafeHandle Next(Timespec deadline)
		{
            return grpc_completion_queue_next(handle, deadline);
		}

		public EventSafeHandle Pluck(IntPtr tag, Timespec deadline)
		{
            return grpc_completion_queue_pluck(handle, tag, deadline);
		}
	}

	internal class CompletionQueueSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc_csharp_ext.so")]
        static extern void grpc_completion_queue_shutdown_drain_destroy(IntPtr cq);

		protected override bool ReleaseHandle()
		{
            grpc_completion_queue_shutdown_drain_destroy(handle);
			return true;
		}
	}
}

