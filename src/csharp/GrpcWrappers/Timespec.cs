using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
	/// <summary>
	/// gpr_timespec from grpc/support/time.h
	/// </summary>
	[StructLayout(LayoutKind.Sequential)]
	internal struct Timespec
	{
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
	}
}

