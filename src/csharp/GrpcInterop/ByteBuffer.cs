using System;
using System.Runtime.InteropServices;
using System.IO;

namespace Google.GRPC.Interop
{
	public class ByteBuffer : WrappedNative
	{
		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_byte_buffer_create(Slice[] slices, UIntPtr nslices);

		[DllImport("libgrpc.so")]
		static extern UIntPtr grpc_byte_buffer_length(IntPtr byteBuffer);

		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_destroy(IntPtr byteBuffer);

		[DllImport("libgrpc.so")]
		static extern IntPtr grpc_byte_buffer_reader_create(IntPtr buffer);
		// returns grpc_byte_buffer_reader *
		// TODO: what is the size of returned int?
		[DllImport("libgrpc.so")]
		static extern int grpc_byte_buffer_reader_next(IntPtr reader, IntPtr slice);

		[DllImport("libgrpc.so")]
		static extern void grpc_byte_buffer_reader_destroy(IntPtr reader);

		public ByteBuffer(Slice[] slices) : 
			base(() => grpc_byte_buffer_create(slices, new UIntPtr((ulong) slices.Length)))
		{
			//TODO: we need to unref the slices!!!
		}

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

		/// <summary>
		/// Reads all data from the byte buffer (does not take ownership of the byte buffer).
		/// </summary>
		public static byte[] ReadByteBuffer(IntPtr byteBuffer)
		{
			using (MemoryStream ms = new MemoryStream())
			{
				IntPtr reader = IntPtr.Zero;
				try
				{
					reader = grpc_byte_buffer_reader_create(byteBuffer);
					byte[] sliceData;
					while ((sliceData = ByteBufferReadNext(reader)) != null)
					{
						ms.Write(sliceData, 0, sliceData.Length);
					}
					return ms.ToArray();
				}
				finally
				{
					if (reader != IntPtr.Zero)
					{
						grpc_byte_buffer_reader_destroy(reader);
					}
				}
			}
		}

		/// <summary>
		/// Reads data of next slice from the byte buffer.
		/// Returns null if there is end of buffer has been reached.
		/// </summary>
		/// <param name="byteBufferReader">Byte buffer reader (does not take ownership)</param>
		private static byte[] ByteBufferReadNext(IntPtr byteBufferReader)
		{
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
}