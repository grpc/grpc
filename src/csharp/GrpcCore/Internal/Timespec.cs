using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace Google.GRPC.Core.Internal
{
	/// <summary>
	/// gpr_timespec from grpc/support/time.h
	/// </summary>
	[StructLayout(LayoutKind.Sequential)]
	internal struct Timespec
	{
        const int nanosPerSecond = 1000 * 1000 * 1000;
        const int nanosPerTick = 100;

        [DllImport("grpc_csharp_ext.dll")]
        static extern Timespec gprsharp_now();

        [DllImport("grpc_csharp_ext.dll")]
        static extern Timespec gprsharp_inf_future();

        [DllImport("grpc_csharp_ext.dll")]
        static extern int gprsharp_sizeof_timespec();

        // TODO: revisit this.
		// NOTE: on linux 64bit  sizeof(gpr_timespec) = 16, on windows 32bit sizeof(gpr_timespec) = 8
        // so IntPtr seems to have the right size to work on both.
		public System.IntPtr tv_sec;
		public System.IntPtr tv_nsec;

		/// <summary>
		/// Timespec a long time in the future.
		/// </summary>
		public static Timespec InfFuture
		{
			get
			{
                return gprsharp_inf_future();
			}
		}

        public static Timespec Now
        {
            get
            {
                return gprsharp_now();
            }
        }

        internal static int NativeSize
        {
            get
            {
                return gprsharp_sizeof_timespec();
            }
        }

        /// <summary>
        /// Creates a GPR deadline from current instant and given timeout.
        /// </summary>
        /// <returns>The from timeout.</returns>
        public static Timespec DeadlineFromTimeout(TimeSpan timeout) {
            if (timeout == Timeout.InfiniteTimeSpan)
            {
                return Timespec.InfFuture;
            }
            return Timespec.Now.Add(timeout);
        }

        public Timespec Add(TimeSpan timeSpan) {
            long nanos = tv_nsec.ToInt64() + (timeSpan.Ticks % TimeSpan.TicksPerSecond) * nanosPerTick;
            long overflow_sec = (nanos > nanosPerSecond) ? 1 : 0;

            Timespec result;
            result.tv_nsec = new IntPtr(nanos % nanosPerSecond);
            result.tv_sec = new IntPtr(tv_sec.ToInt64() + (timeSpan.Ticks / TimeSpan.TicksPerSecond) + overflow_sec); 
            return result;
        }
	}
}

