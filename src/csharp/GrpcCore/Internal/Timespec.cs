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

        [DllImport("libgpr.so")]
        static extern Timespec gpr_now();

		// TODO: this only works on 64bit linux, can we autoselect the right size of ints?
		// perhaps using IntPtr would work.
		public System.Int64 tv_sec;
		public System.Int64 tv_nsec;

		/// <summary>
		/// Timespec a long time in the future.
		/// </summary>
		public static Timespec InfFuture
		{
			get
			{
				// TODO: set correct value based on the length of the struct
				return new Timespec { tv_sec = Int32.MaxValue, tv_nsec = 0 };
			}
		}

        public static Timespec Now
        {
            get
            {
                return gpr_now();
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
            long nanos = tv_nsec + (timeSpan.Ticks % TimeSpan.TicksPerSecond) * nanosPerTick;
            long overflow_sec = (nanos > nanosPerSecond) ? 1 : 0;

            Timespec result;
            result.tv_nsec = nanos % nanosPerSecond;
            result.tv_sec = tv_sec + (timeSpan.Ticks / TimeSpan.TicksPerSecond) + overflow_sec; 
            return result;
        }
	}
}

