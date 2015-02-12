using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// grpc_completion_queue from <grpc/grpc.h>
    /// </summary>
	internal class CompletionQueueSafeHandle : SafeHandleZeroIsInvalid
	{
        [DllImport("grpc.dll")]
        static extern CompletionQueueSafeHandle grpc_completion_queue_create();

        [DllImport("grpc.dll")]
        static extern EventSafeHandle grpc_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag, Timespec deadline);

        [DllImport("grpc.dll")]
        static extern EventSafeHandle grpc_completion_queue_next(CompletionQueueSafeHandle cq, Timespec deadline);

        [DllImport("grpc.dll")]
        static extern void grpc_completion_queue_shutdown(CompletionQueueSafeHandle cq);

        [DllImport("grpc_csharp_ext.dll")]
        static extern GRPCCompletionType grpc_completion_queue_next_with_callback(CompletionQueueSafeHandle cq);

        [DllImport("grpc.dll")]
        static extern void grpc_completion_queue_destroy(IntPtr cq);

        private CompletionQueueSafeHandle()
        {
        }

        public static CompletionQueueSafeHandle Create()
        {
            return grpc_completion_queue_create();
        }

        public EventSafeHandle Next(Timespec deadline)
        {
            return grpc_completion_queue_next(this, deadline);
        }

        public GRPCCompletionType NextWithCallback()
        {
            return grpc_completion_queue_next_with_callback(this);
        }

        public EventSafeHandle Pluck(IntPtr tag, Timespec deadline)
        {
            return grpc_completion_queue_pluck(this, tag, deadline);
        }

        public void Shutdown()
        {
            grpc_completion_queue_shutdown(this);
        }

		protected override bool ReleaseHandle()
        {
            grpc_completion_queue_destroy(handle);
			return true;
		}
	}
}

