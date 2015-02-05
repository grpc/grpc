using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core
{
	public class Channel : IDisposable
	{
        /// <summary>
        /// Make sure GPRC environment is initialized before any channels get used.
        /// </summary>
        static Channel() {
            GrpcEnvironment.EnsureInitialized();
        }
       
        readonly ChannelSafeHandle handle;
        readonly String target;

        // TODO: add way how to create grpc_secure_channel....
		// TODO: add support for channel args...
		public Channel(string target)
		{
            this.handle = ChannelSafeHandle.Create(target, IntPtr.Zero);
			this.target = target;
		}

        internal ChannelSafeHandle Handle
        {
            get
            {
                return this.handle;
            }
        }

        public string Target
        {
            get
            {
                return this.target;
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (handle != null && !handle.IsInvalid)
            {
                handle.Dispose();
            }
        }
	}
}