using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
    /// <summary>
    /// gpr_slice from grpc/support/slice.h
    /// 
    /// Working with slices is a bit tricky, because they need you to call Unref() when
    /// disposing them, but we can't implement safe disposal using SafeHandle, because
    /// gpr_slice is a struct rather than a simple pointer.
    /// Avoid working with Slice if possible (or make sure you understand the
    /// disposal logic).
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

        /// <summary>
        /// Returns true if this slice is refcounted and therefore needs Unref() call
        /// when disposed.
        /// </summary>
        public bool IsRefcounted
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

		public static Slice CreateEmpty()
		{
			return gpr_empty_slice();
		}

        /// <summary>
        /// You need to call Unref() once done with the slice.
        /// </summary>
		public static Slice CreateFromString(string s)
		{
			return gpr_slice_from_copied_string(s);
		}

        /// <summary>
        /// You need to call Unref() once done with the slice.
        /// </summary>
		public static Slice CreateFromByteArray(byte[] data)
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

