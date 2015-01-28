using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Google.GRPC.Wrappers
{
	public class Channel : WrappedNativeObject<ChannelSafeHandle>
	{
        /// <summary>
        /// Make sure GPRC environment is initialized before any channels get used.
        /// </summary>
        static Channel() {
            GrpcEnvironment.EnsureInitialized();
        }

		[DllImport("libgrpc.so")]
		static extern ChannelSafeHandle grpc_channel_create(string target, IntPtr channelArgs);

		readonly String target;

		// TODO: add support for channel args...
		public Channel(string target) : base(grpc_channel_create(target, IntPtr.Zero))
		{
			this.target = target;
		}

		public CallContext CreateCallContext(String methodName)
		{
			Call call = new Call(this, methodName, target, CreateDeadline());
			return new CallContext(call);
		}

        private Timespec CreateDeadline() {
            // TODO: create deadline based on stub configuration. For now, no deadline is used.
            return Timespec.InfFuture;
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