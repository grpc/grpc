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
		public Channel(string target) : base(() => grpc_channel_create(target, IntPtr.Zero))
		{
			this.target = target;
		}

		public Status SimpleBlockingCall(String methodName, byte[] requestData, out byte[] result, Timespec deadline)
		{
			result = null;
			Call call = new Call(this, methodName, target, deadline);
			using (CallContext ctx = new CallContext(call))
			{
				ctx.Start(false);

				if (!ctx.Write(requestData))
				{
					return ctx.Wait();
				}
				ctx.WritesDone();

				result = ctx.Read();

				return ctx.Wait();
			}
		}

		public CallContext StreamingCall(String methodName, Timespec deadline)
		{
			Call call = new Call(this, methodName, target, deadline);
			return new CallContext(call);
		}

		protected override void Destroy()
		{
			grpc_channel_destroy(RawPointer);
		}
	}
}