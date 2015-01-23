using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{

	// TODO: this only works on 64bit linux
	[StructLayout(LayoutKind.Sequential)]
	public struct GPRTimespec {
		public System.Int64 tv_sec;
		public System.Int64 tv_nsec;

		public static GPRTimespec InfFuture
		{
			get {
				// TODO: set correct value.
				return new GPRTimespec { tv_sec = Int32.MaxValue, tv_nsec = 0 };
			}
		}
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct GPRSlice {

		[DllImport("libgpr.so")]
		static extern GPRSlice gpr_empty_slice();

		[DllImport("libgpr.so")]
		static extern GPRSlice gpr_slice_from_copied_string(string source);
	
		[DllImport("libgpr.so")]
		static extern GPRSlice gpr_slice_from_copied_buffer(byte[] data, UIntPtr len);

		IntPtr refcount;
		Data data;

		public int Length {
			get {
				return (refcount != IntPtr.Zero) ? (int) data.refcounted.length : (int) data.inlined.length;
			}
		}

		public byte[] GetDataAsByteArray()
		{
			if (refcount != IntPtr.Zero) {
				int len = (int) data.refcounted.length;
				byte[] result = new byte[len];
				Marshal.Copy(data.refcounted.bytes, result, 0, len);
				return result;
			} else {
				int len = (int) data.inlined.length;
				byte[] result = new byte[len];
				Buffer.BlockCopy (data.inlined.bytes, 0, result, 0, len);
				return result;
			}
		}


		public static GPRSlice Empty {
			get {
				return gpr_empty_slice();
			}
		}

		public static GPRSlice FromString(string s) {
			return gpr_slice_from_copied_string(s);
		}

		public static GPRSlice FromByteArray(byte[] data) {
			return gpr_slice_from_copied_buffer(data, new UIntPtr((ulong) data.Length));
		}


		[StructLayout(LayoutKind.Explicit)]
		public struct Data {
			[FieldOffset(0)]
			public RefcountedData refcounted;

			[FieldOffset(0)]
			public InlinedData inlined;
		}

		[StructLayout(LayoutKind.Sequential)]
		public struct RefcountedData {
			public IntPtr bytes;
			public UIntPtr length;
		}

		[StructLayout(LayoutKind.Sequential, Pack = 1)]
		public struct InlinedData {
			public byte length;

			// TODO: sizeconst depend on whether we are 32 bit or 64 bit!!!!
			[MarshalAs(
				UnmanagedType.ByValArray,
				SizeConst=15)]
			public byte[] bytes;
		}
	}
}

