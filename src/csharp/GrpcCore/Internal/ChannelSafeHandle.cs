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
        [DllImport("grpc_csharp_ext.dll")]
        static extern ChannelSafeHandle grpcsharp_channel_create(string target, IntPtr channelArgs);

		[DllImport("grpc_csharp_ext.dll")]
		static extern void grpcsharp_channel_destroy(IntPtr channel);

        private ChannelSafeHandle()
        {
        }

        public static ChannelSafeHandle Create(string target, IntPtr channelArgs)
        {
            return grpcsharp_channel_create(target, channelArgs);
        }

		protected override bool ReleaseHandle()
		{
			grpcsharp_channel_destroy(handle);
			return true;
		}
	}
}