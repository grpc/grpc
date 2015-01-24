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
		static extern IntPtr grpc_completion_queue_next(IntPtr cq, Timespec deadline);
		// returns grpc_event*
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_completion_queue_pluck(IntPtr cq, IntPtr tag, Timespec deadline);

		[DllImport("libgrpc.so")]
		static extern void grpc_completion_queue_shutdown(IntPtr cq);

		[DllImport("libgrpc.so")]
		static extern void grpc_completion_queue_destroy(IntPtr cq);

		[DllImport("libgrpc.so")]
		static extern void grpc_event_finish(IntPtr ev);

		private readonly bool autoShutdown;

		public CompletionQueue(bool autoShutdown) : base(() => grpc_completion_queue_create())
		{
			this.autoShutdown = autoShutdown;
		}

		public Event Next(Timespec deadline)
		{
			IntPtr eventPtr = IntPtr.Zero;
			try
			{
				eventPtr = grpc_completion_queue_next(RawPointer, deadline);
				if (eventPtr == IntPtr.Zero)
				{
					return null;
				}
				return new Event(eventPtr);
			}
			finally
			{
				if (eventPtr != IntPtr.Zero)
				{
					grpc_event_finish(eventPtr);
				}
			}
		}

		public Event Pluck(IntPtr tag, Timespec deadline)
		{
			IntPtr eventPtr = IntPtr.Zero;
			try
			{
				eventPtr = grpc_completion_queue_pluck(RawPointer, tag, deadline);
				if (eventPtr == IntPtr.Zero)
				{
					return null;
				}
				return new Event(eventPtr);
			}
			finally
			{
				if (eventPtr != IntPtr.Zero)
				{
					grpc_event_finish(eventPtr);
				}
			}
		}

		public void Shutdown()
		{
			grpc_completion_queue_shutdown(RawPointer);
		}

		protected override void Destroy()
		{
			if (autoShutdown)
			{
				ShutdownAndDrain();
			}
			grpc_completion_queue_destroy(RawPointer);
		}

		private void ShutdownAndDrain()
		{
			// WARNING: be careful that Next can't be mixed with Pluck (see gprc.h)
			grpc_completion_queue_shutdown(RawPointer);
			while (Next(Timespec.InfFuture).CompletionType != GRPCCompletionType.GRPC_QUEUE_SHUTDOWN)
			{
			}
		}
	}
}

