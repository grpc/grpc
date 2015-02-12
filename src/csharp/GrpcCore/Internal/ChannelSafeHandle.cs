using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// grpc_channel from <grpc/grpc.h>
    /// </summary>
	internal class ChannelSafeHandle : SafeHandleZeroIsInvalid
	{
        [DllImport("grpc.dll")]
        static extern ChannelSafeHandle grpc_channel_create(string target, IntPtr channelArgs);

		[DllImport("grpc.dll")]
		static extern void grpc_channel_destroy(IntPtr channel);

        private ChannelSafeHandle()
        {
        }

        public static ChannelSafeHandle Create(string target, IntPtr channelArgs)
        {
            return grpc_channel_create(target, channelArgs);
        }

		protected override bool ReleaseHandle()
		{
			grpc_channel_destroy(handle);
			return true;
		}
	}
}