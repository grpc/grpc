using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
	public class CallContext : IDisposable
	{
		IntPtr invoke_tag = new IntPtr(1);
		IntPtr metadata_read_tag = new IntPtr(2);
		IntPtr finished_tag = new IntPtr(3);
		IntPtr write_tag = new IntPtr(4);
		IntPtr halfclose_tag = new IntPtr(5);
		IntPtr read_tag = new IntPtr(6);
		readonly Call call;
		readonly CompletionQueue cq;
		bool disposed = false;

		public CallContext(Call call)
		{
			this.call = call;
			this.cq = new CompletionQueue(true);
		}

		~CallContext()
		{
			Dispose(false);
		}

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (!disposed)
			{
				if (disposing)
				{
					try
					{
						call.Dispose();
					}
					finally
					{
						cq.Dispose();
					}
				} 
				disposed = true;
			}
		}

		public void Start(bool buffered)
		{
			UInt32 flags = buffered ? 0 : GRPCUtils.GRPC_WRITE_BUFFER_HINT;
			Utils.AssertCallOk(call.StartInvoke(cq, invoke_tag, metadata_read_tag, finished_tag, flags));
			cq.Pluck(invoke_tag, GPRTimespec.InfFuture);
		}

		// blocking write...
		public bool Write(byte[] payload)
		{
			//TODO: we need to call unref on the slice!!!
			using (ByteBuffer byteBuffer = new ByteBuffer(new GPRSlice[] { GPRSlice.FromByteArray(payload)}))
			{
				Utils.AssertCallOk(call.StartWrite(byteBuffer, write_tag, GRPCUtils.GRPC_WRITE_BUFFER_HINT));
			}
			Event writeEvent = cq.Pluck(write_tag, GPRTimespec.InfFuture);
			return (writeEvent.WriteAcceptedSuccess == GRPCOpError.GRPC_OP_OK);
		}

		public void WritesDone()
		{
			Utils.AssertCallOk(call.WritesDone(halfclose_tag));
			cq.Pluck(halfclose_tag, GPRTimespec.InfFuture);
		}

		public void Cancel()
		{
			Utils.AssertCallOk(call.Cancel());
		}

		public Status Wait()
		{
			Event ev = cq.Pluck(finished_tag, GPRTimespec.InfFuture);
			return ev.FinishedStatus;
		}

		// blocking read...
		public byte[] Read()
		{
			Utils.AssertCallOk(call.StartRead(read_tag));
			Event readEvent = cq.Pluck(read_tag, GPRTimespec.InfFuture);
			return readEvent.ReadData;
		}

		//TODO: metadata read and write
	}
}