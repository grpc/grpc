using System;
using System.Runtime.InteropServices;
using System.IO;

namespace Google.GRPC.Interop
{
	public class ByteBuffer : WrappedNativeObject<ByteBufferSafeHandle>
	{
		[DllImport("libgrpc.so")]
		static extern ByteBufferSafeHandle grpc_byte_buffer_create(Slice[] slices, UIntPtr nslices);

		[DllImport("libgrpc.so")]
		static extern UIntPtr grpc_byte_buffer_length(ByteBufferSafeHandle byteBuffer);

		// buffer pointer is IntPtr because we obtain that in the read event.
		[DllImport("libgrpc.so")]
		static extern ByteBufferReaderSafeHandle grpc_byte_buffer_reader_create(IntPtr buffer);

		// TODO: what is the size of returned int?
		[DllImport("libgrpc.so")]
		static extern int grpc_byte_buffer_reader_next(ByteBufferReaderSafeHandle reader, IntPtr slice);

		public ByteBuffer(Slice[] slices)
			: base(grpc_byte_buffer_create(slices, new UIntPtr((ulong) slices.Length)))
		{
			//TODO: we need to unref the slices!!!
		}

		public UIntPtr Length
		{
			get
			{
				return grpc_byte_buffer_length(handle);
			}
		}

		/// <summary>
		/// Reads all data from the byte buffer (does not take ownership of the byte buffer).
		/// </summary>
		public static byte[] ReadByteBuffer(IntPtr byteBuffer)
		{	
			using (MemoryStream ms = new MemoryStream())
			{
				using (ByteBufferReaderSafeHandle reader = grpc_byte_buffer_reader_create(byteBuffer))
				{
					byte[] sliceData;
					while ((sliceData = ByteBufferReadNext(reader)) != null)
					{
						ms.Write(sliceData, 0, sliceData.Length);
					}
					return ms.ToArray();
				}
			}
		}

		/// <summary>
		/// Reads data of next slice from the byte buffer.
		/// Returns null if there is end of buffer has been reached.
		/// </summary>
		private static byte[] ByteBufferReadNext(ByteBufferReaderSafeHandle byteBufferReader)
		{
			// TODO: inspect this....

			IntPtr slicePtr = IntPtr.Zero;
			try
			{
				slicePtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(Slice)));
				if (grpc_byte_buffer_reader_next(byteBufferReader, slicePtr) == 0)
				{
					return null;
				}

				Slice slice = default(Slice);
				try
				{
					slice = (Slice)Marshal.PtrToStructure(slicePtr, typeof(Slice));
					return slice.GetDataAsByteArray();

				}
				finally
				{
					if (slice.NeedsUnref)
					{
						slice.Unref();
					}
				}
			}
			finally
			{
				if (slicePtr != IntPtr.Zero)
				{
					Marshal.FreeHGlobal(slicePtr);
				}
			}
		}
	}

	public class ByteBufferSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_destroy(IntPtr byteBuffer);

		protected override bool ReleaseHandle()
		{
			grpc_byte_buffer_destroy(handle);
			return true;
		}
	}

	public class ByteBufferReaderSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_reader_destroy(IntPtr reader);

		protected override bool ReleaseHandle()
		{
			grpc_byte_buffer_reader_destroy(handle);
			return true;
		}
	}
}