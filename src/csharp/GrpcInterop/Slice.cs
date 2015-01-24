using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
    /// <summary>
    /// gpr_slice from grpc/support/slice.h
    /// </summary>
	[StructLayout(LayoutKind.Sequential)]
	public struct Slice
	{
		[DllImport("libgpr.so")]
		static extern Slice gpr_empty_slice();

		[DllImport("libgpr.so")]
		static extern Slice gpr_slice_from_copied_string(string source);

		[DllImport("libgpr.so")]
		static extern Slice gpr_slice_from_copied_buffer(byte[] data, UIntPtr len);

        [DllImport("libgpr.so")]
        static extern Slice gpr_slice_unref(Slice slice);

		IntPtr refcount;
		Data data;

        //TODO: figure out an ellegant way to do Unrefs.

        //TODO: this should basically return true if this != default(Slice).
        //TODO: rename this!
        public bool NeedsUnref
        {
            get
            {
                return (refcount != IntPtr.Zero);
            }
        }

		public int Length
		{
			get
			{
				return (refcount != IntPtr.Zero) ? (int)data.refcounted.length : (int)data.inlined.length;
			}
		}

		public byte[] GetDataAsByteArray()
		{
			if (refcount != IntPtr.Zero)
			{
				int len = (int)data.refcounted.length;
				byte[] result = new byte[len];
				Marshal.Copy(data.refcounted.bytes, result, 0, len);
				return result;
			}
			else
			{
				int len = (int)data.inlined.length;
				byte[] result = new byte[len];
				Buffer.BlockCopy(data.inlined.bytes, 0, result, 0, len);
				return result;
			}
		}

		public static Slice Empty
		{
			get
			{
				return gpr_empty_slice();
			}
		}

		public static Slice FromString(string s)
		{
			return gpr_slice_from_copied_string(s);
		}

		public static Slice FromByteArray(byte[] data)
		{
			return gpr_slice_from_copied_buffer(data, new UIntPtr((ulong)data.Length));
		}

        public void Unref()
        {
            gpr_slice_unref(this);
        }

		[StructLayout(LayoutKind.Explicit)]
		public struct Data
		{
			[FieldOffset(0)]
			public RefcountedData refcounted;
			[FieldOffset(0)]
			public InlinedData inlined;
		}

		[StructLayout(LayoutKind.Sequential)]
		public struct RefcountedData
		{
			public IntPtr bytes;
			public UIntPtr length;
		}

		[StructLayout(LayoutKind.Sequential, Pack = 1)]
		public struct InlinedData
		{
			public byte length;
			// TODO: sizeconst depend on whether we are 32 bit or 64 bit!!!!
			[MarshalAs(
				UnmanagedType.ByValArray,
				SizeConst=15)]
			public byte[] bytes;
		}
	}
}

