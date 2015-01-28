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

        public string Target
        {
            get
            {
                return this.target;
            }
        }

        internal Call CreateCall(String methodName, TimeSpan timeout)
        {
            return new Call(this, methodName, target, CreateDeadline(timeout));
        }

        private Timespec CreateDeadline(TimeSpan timeout) {
            if (timeout == Timeout.InfiniteTimeSpan)
            {
                return Timespec.InfFuture;
            }

            // TODO: convert TimeSpan to timespec.
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