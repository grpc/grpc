using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
	/// <summary>
	/// gpr_timespec from grpc/support/time.h
	/// </summary>
	[StructLayout(LayoutKind.Sequential)]
	public struct Timespec
	{
		// TODO: this only works on 64bit linux, can we autoselect the right size of ints?
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

