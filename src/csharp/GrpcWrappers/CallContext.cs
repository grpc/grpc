using System;
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace Google.GRPC.Wrappers
{
    public interface ICallContext : IDisposable {
    
        bool Write(byte[] payload);

        void WritesDone();

        void Cancel();

        Status Wait();

        byte[] Read();

        //TODO: metadata read and write
    }

	public class CallContext : ICallContext
	{
        // Any set of distinct values for tags will do.
		IntPtr metadata_read_tag = new IntPtr(2);
		IntPtr finished_tag = new IntPtr(3);
		IntPtr write_tag = new IntPtr(4);
		IntPtr halfclose_tag = new IntPtr(5);
		IntPtr read_tag = new IntPtr(6);

        readonly object myLock = new object();
		readonly Call call;
		readonly CompletionQueue cq;
		bool disposed = false;
        int refcount = 1;

		public CallContext(Call call)
		{
			this.call = call;
			this.cq = new CompletionQueue();
		}

        /// <summary>
        /// Adds a reference to this call context.
        /// The call context will be disposed the parent CallContext
        /// and all the references are disposed.
        /// </summary>
        /// <returns>The reference.</returns>
        public ICallContext AddRef() {
            lock (myLock)
            {
                refcount ++;
                return new CallContextReference(this);
            }
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
                    RemoveRef();
				} 
				disposed = true;
			}
		}

		//TODO: access to call methods might need to be synchronized.

		public void Start(bool buffered)
		{
			Utils.AssertCallOk(call.Invoke(cq, metadata_read_tag, finished_tag, buffered));
		}

		// blocking write...
		public bool Write(byte[] payload)
		{
			using (ByteBuffer byteBuffer = new ByteBuffer(payload))
			{
				Utils.AssertCallOk(call.StartWrite(byteBuffer, write_tag, false));
			}
			Event writeEvent = cq.Pluck(write_tag, Timespec.InfFuture);
			return (writeEvent.WriteAcceptedSuccess == GRPCOpError.GRPC_OP_OK);
		}

		public void WritesDone()
		{
			Utils.AssertCallOk(call.WritesDone(halfclose_tag));
			cq.Pluck(halfclose_tag, Timespec.InfFuture);
		}

		public void Cancel()
		{
			Utils.AssertCallOk(call.Cancel());
		}

		public Status Wait()
		{
			Event ev = cq.Pluck(finished_tag, Timespec.InfFuture);
			return ev.FinishedStatus;
		}

		// blocking read...
		public byte[] Read()
		{
			Utils.AssertCallOk(call.StartRead(read_tag));
			Event readEvent = cq.Pluck(read_tag, Timespec.InfFuture);
			return readEvent.ReadData;
		}

        private void RemoveRef() {

            bool shouldRelease = false;

            lock (myLock)
            {
                Trace.Assert(refcount > 0, "Reference count is corrupt");
                refcount --;
                shouldRelease = (refcount == 0);
            }

            if (shouldRelease)
            {
                ReleaseResources();
            }
        }

        private void ReleaseResources() {
            // dispose both call and completion queue...
            try
            {
                call.Dispose();
            }
            finally
            {
                cq.Dispose();
            }
        }

		private class CallContextReference : ICallContext {

            readonly CallContext parent;
            bool disposed;

            public CallContextReference(CallContext parent) {
                this.parent = parent;
            }

            public bool Write(byte[] payload)
            {
                return parent.Write(payload);
            }

            public void WritesDone()
            {
                parent.WritesDone();
            }

            public void Cancel()
            {
                parent.Cancel();
            }

            public Status Wait()
            {
                return parent.Wait();
            }

            public byte[] Read()
            {
                return parent.Read();
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
                        parent.RemoveRef();
                    } 
                    disposed = true;
                }
            }
        }
	}
}