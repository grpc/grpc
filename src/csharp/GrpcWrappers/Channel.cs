using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Google.GRPC.Wrappers
{
	public class Channel : WrappedNativeObject<ChannelSafeHandle>
	{
		[DllImport("libgrpc.so")]
		static extern ChannelSafeHandle grpc_channel_create(string target, IntPtr channelArgs);

		readonly String target;

		// TODO: add support for channel args...
		public Channel(string target) : base(grpc_channel_create(target, IntPtr.Zero))
		{
			this.target = target;
		}

		public Status SimpleBlockingCall(String methodName, byte[] requestData, out byte[] result, Timespec deadline, CancellationToken token)
		{
			// TODO: implement cancellation

			result = null;

			// TODO: strange way of disposing call here
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

		public CallContext CreateCall(String methodName, Timespec deadline)
		{
			Call call = new Call(this, methodName, target, deadline);
			return new CallContext(call);
		}
	}

	public class ChannelSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc.so")]
		static extern void grpc_channel_destroy(IntPtr channel);

		protected override bool ReleaseHandle()
		{
			grpc_channel_destroy(handle);
			return true;
		}
	}
}