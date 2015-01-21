using System;
using System.Runtime.InteropServices;
using System.IO;

namespace Google.GRPC.Interop
{
	public class ByteBuffer : WrappedNative
	{
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_byte_buffer_create(GPRSlice[] slices, UIntPtr nslices);

		[DllImport("libgrpc.so")]
		static extern UIntPtr grpc_byte_buffer_length(IntPtr byteBuffer);

		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_destroy(IntPtr byteBuffer);

		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_byte_buffer_reader_create(IntPtr buffer); // returns grpc_byte_buffer_reader *

		// TODO: what is the size of returned int?
		[DllImport("libgrpc.so")]
		static extern int grpc_byte_buffer_reader_next(IntPtr reader, IntPtr slice);

		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_reader_destroy(IntPtr reader);

		[DllImport("libgpr.so")]
		static extern GPRSlice gpr_slice_unref(GPRSlice slice);

		public ByteBuffer(GPRSlice[] slices) : 
			base(grpc_byte_buffer_create(slices, new UIntPtr((ulong) slices.Length))) {}

		public UIntPtr Length
		{
			get
			{
				return grpc_byte_buffer_length(RawPointer);
			}
		}

		protected override void Destroy()
		{
			grpc_byte_buffer_destroy(RawPointer);
		}

		// reads all data from the byte buffer (without destroying it).
		public static byte[] ReadByteBuffer(IntPtr byteBuffer) {
			// TODO: disposing of the byte buffer reader...
			MemoryStream result = new MemoryStream();

			IntPtr reader = grpc_byte_buffer_reader_create(byteBuffer);
			IntPtr slicePtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(GPRSlice)));

			while (grpc_byte_buffer_reader_next(reader, slicePtr) != 0) {
				GPRSlice slice = (GPRSlice) Marshal.PtrToStructure(slicePtr, typeof(GPRSlice));
				Marshal.FreeHGlobal(slicePtr);
				byte[] sliceData = slice.GetDataAsByteArray();
				result.Write(sliceData, 0, sliceData.Length);
				gpr_slice_unref(slice);

			}

			grpc_byte_buffer_reader_destroy(reader);
			return result.ToArray();
		}
	}
}