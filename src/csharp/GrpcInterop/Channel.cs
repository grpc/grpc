using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
	public class Channel : WrappedNative
	{
		// returns grpc_channel*
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_channel_create(string target, IntPtr channelArgs);

		[DllImport("libgrpc.so")]
		static extern void grpc_channel_destroy(IntPtr channel);

		readonly String target;

		// TODO: add args...
		public Channel(string target) : base(grpc_channel_create(target, IntPtr.Zero))
		{
			this.target = target;
		}

		public Status StartBlockingRpc(String methodName, byte[] requestData, out byte[] result, GPRTimespec deadline) {

			result = null;
			using(Call call = new Call(this, methodName, target, deadline))
			{
			    IntPtr invoke_tag = new IntPtr (1);
				IntPtr metadata_read_tag = new IntPtr (2);
				IntPtr finished_tag = new IntPtr (3);
				IntPtr write_tag = new IntPtr (4);
				IntPtr halfclose_tag = new IntPtr (5);
				IntPtr read_tag = new IntPtr (6);

				using (CompletionQueue cq = new CompletionQueue(true)) {
					Utils.AssertCallOk(call.StartInvoke(cq, invoke_tag, metadata_read_tag, finished_tag, GRPCUtils.GRPC_WRITE_BUFFER_HINT));
					cq.Pluck (invoke_tag, GPRTimespec.GPRInfFuture);

					using (ByteBuffer byteBuffer = new ByteBuffer(new GPRSlice[] { GPRSlice.FromByteArray(requestData)}))
					{
						Utils.AssertCallOk (call.StartWrite (byteBuffer, write_tag, GRPCUtils.GRPC_WRITE_BUFFER_HINT));
					}

					Event writeEvent = cq.Pluck (write_tag, GPRTimespec.GPRInfFuture);
					if (writeEvent.WriteAcceptedSuccess != GRPCOpError.GRPC_OP_OK) {
						return GetFinalStatus (cq, finished_tag);
					}

					// writes are done
					Utils.AssertCallOk(call.WritesDone (halfclose_tag));
					cq.Pluck (halfclose_tag, GPRTimespec.GPRInfFuture);

					// start read metadata
					cq.Pluck (metadata_read_tag, GPRTimespec.GPRInfFuture);

					// start read
					Utils.AssertCallOk(call.StartRead(read_tag));
					Event readEvent = cq.Pluck (read_tag, GPRTimespec.GPRInfFuture);
					result = readEvent.ReadData;

					return GetFinalStatus(cq, finished_tag);
				}
			}
		}

		private Status GetFinalStatus(CompletionQueue cq, IntPtr finished_tag) {
			Event ev = cq.Pluck (finished_tag, GPRTimespec.GPRInfFuture);
			return ev.FinishedStatus;
		}

		protected override void Destroy() {
			grpc_channel_destroy(RawPointer);
		}
	}
}