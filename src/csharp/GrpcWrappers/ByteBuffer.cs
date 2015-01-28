using System;
using System.Runtime.InteropServices;
using System.IO;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// Wrapper to work with native grpc_byte_buffer.
    /// </summary>
	internal class ByteBuffer : WrappedNativeObject<ByteBufferSafeHandle>
	{
		[DllImport("libgrpc.so")]
		static extern ByteBufferSafeHandle grpc_byte_buffer_create(Slice[] slices, UIntPtr nslices);

		[DllImport("libgrpc.so")]
		static extern UIntPtr grpc_byte_buffer_length(ByteBufferSafeHandle byteBuffer);

		[DllImport("libgrpc.so")]
		static extern ByteBufferReaderSafeHandle grpc_byte_buffer_reader_create(IntPtr buffer);

		// TODO: what is the size of returned int?
		[DllImport("libgrpc.so")]
		static extern int grpc_byte_buffer_reader_next(ByteBufferReaderSafeHandle reader, IntPtr slice);

		public ByteBuffer(byte[] data) : base(CreateByteBufferSafeHandleFromByteArray(data))
		{
		}

		public UIntPtr Length
		{
			get
			{
				return grpc_byte_buffer_length(handle);
			}
		}

		private static ByteBufferSafeHandle CreateByteBufferSafeHandleFromByteArray(byte[] data)
		{
			Slice slice = default(Slice);
			try
			{
				slice = Slice.CreateFromByteArray(data);
				return grpc_byte_buffer_create(new Slice[] {slice}, new UIntPtr(1));

			} finally
			{
				if (slice.IsRefcounted) {
					slice.Unref();
				}
			}
		}

		// TODO: we are using IntPtr for byteBuffer because we obtain that in the read event.
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
		/// Returns null if end of buffer has been reached.
		/// </summary>
		private static byte[] ByteBufferReadNext(ByteBufferReaderSafeHandle byteBufferReader)
		{
			IntPtr slicePtr = IntPtr.Zero;
			try
			{
				// TODO: is there a nicer way how to accomplish this?
				// Allocate memory where the resulting slice will be saved.
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
					if (slice.IsRefcounted)
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

	internal class ByteBufferSafeHandle : SafeHandleZeroIsInvalid
	{
		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_destroy(IntPtr byteBuffer);

		protected override bool ReleaseHandle()
		{
			grpc_byte_buffer_destroy(handle);
			return true;
		}
	}

	internal class ByteBufferReaderSafeHandle : SafeHandleZeroIsInvalid
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