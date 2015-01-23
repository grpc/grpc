using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
	public class CompletionQueue : WrappedNative
	{
		// returns grpc_completion_queue*
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_completion_queue_create();

		// returns grpc_event*
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_completion_queue_next(IntPtr cq, GPRTimespec deadline);

		// returns grpc_event*
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_completion_queue_pluck(IntPtr cq, IntPtr tag, GPRTimespec deadline);

		[DllImport("libgrpc.so")]
		static extern void grpc_completion_queue_shutdown(IntPtr cq);

		[DllImport("libgrpc.so")]
		static extern void grpc_completion_queue_destroy(IntPtr cq);

		[DllImport("libgrpc.so")]
		static extern void grpc_event_finish(IntPtr ev);

		private readonly bool autoShutdown;

		public CompletionQueue (bool autoShutdown) : base(grpc_completion_queue_create()) {
			this.autoShutdown = autoShutdown;
		}

		public Event Next(GPRTimespec deadline) {
			IntPtr eventPtr = grpc_completion_queue_next (RawPointer, deadline);
			if (eventPtr == IntPtr.Zero) {
				// TODO: maybe throw exception...
				return null;
			}
			return CreateEventFromIntPtr (eventPtr);
		}

		public Event Pluck(IntPtr tag, GPRTimespec deadline) {
			IntPtr eventPtr = grpc_completion_queue_pluck(RawPointer, tag, deadline);
			// TODO: handle this....
			if (eventPtr == IntPtr.Zero) {
				// TODO: maybe throw exception...
				return null;
			}
			return CreateEventFromIntPtr (eventPtr);
		}

		public void Shutdown()
		{
			grpc_completion_queue_shutdown(RawPointer);
		}

		protected override void Destroy()
		{
			if (autoShutdown) {
				ShutdownAndDrain();
			}
			grpc_completion_queue_destroy(RawPointer);
		}

		private void ShutdownAndDrain()
		{
			grpc_completion_queue_shutdown(RawPointer);
			while (Next(GPRTimespec.InfFuture).CompletionType != GRPCCompletionType.GRPC_QUEUE_SHUTDOWN) {
			}
		}

		private static Event CreateEventFromIntPtr(IntPtr eventPtr) {
			Event ev = new Event(GRPCEvent.FromIntPtr(eventPtr));
			grpc_event_finish(eventPtr);
			return ev;
		}
	}
}

