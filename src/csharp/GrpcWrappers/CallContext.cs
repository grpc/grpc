using System;
using System.Runtime.InteropServices;
using System.Diagnostics;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// Call context interface.
    /// </summary>
    public interface ICallContext : IDisposable {
    
        bool Write(byte[] payload);

        void WritesDone();

        void Cancel();

        void CancelWithStatus(Status status);

        Status Wait();

        byte[] Read();

        //TODO: metadata read and write
    }

    /// <summary>
    /// Holds resources for an active call and implements reference counting to
    /// allow more actors to work with the call context while still maintaining
    /// the correct disposal logic.
    /// </summary>
	public class CallContext : ICallContext
	{
        // Any set of distinct values for tags will do.
		static readonly IntPtr metadata_read_tag = new IntPtr(1);
		static readonly IntPtr finished_tag = new IntPtr(2);
		static readonly IntPtr write_tag = new IntPtr(3);
		static readonly IntPtr halfclose_tag = new IntPtr(4);
		static readonly IntPtr read_tag = new IntPtr(5);

        readonly object myLock = new object();
        int refcount = 1;
        bool disposed = false;

        Call call;
        CompletionQueue cq;

		public CallContext()
		{
            // No work should be done here!
		}

        /// <summary>
        /// Initialized the call context with given call.
        /// This is not done in the constructor to provide 
        /// a straightforward way of releasing resources
        /// (with "using" statement) in case there would be
        /// an error initializing.
        /// </summary>
        /// <param name="call">Call.</param>
        public void Initialize(Channel channel, String methodName, TimeSpan timeout)
        {
            lock (myLock)
            {
                cq = new CompletionQueue();
                call = channel.CreateCall(methodName, timeout);
            }
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

		public void Start(bool buffered)
		{
            lock (myLock)
            {
                AssertCallOk(call.Invoke(cq, metadata_read_tag, finished_tag, buffered));
            }
		}

		// blocking write...
		public bool Write(byte[] payload)
		{
			using (ByteBuffer byteBuffer = new ByteBuffer(payload))
			{
                lock (myLock)
                {
                    AssertCallOk(call.StartWrite(byteBuffer, write_tag, false));
                }
			}
			Event writeEvent = cq.Pluck(write_tag, Timespec.InfFuture);
			return (writeEvent.WriteAcceptedSuccess == GRPCOpError.GRPC_OP_OK);
		}

		public void WritesDone()
		{
            lock (myLock)
            {
                AssertCallOk(call.WritesDone(halfclose_tag));
            }
			cq.Pluck(halfclose_tag, Timespec.InfFuture);
		}

		public void Cancel()
		{
            // grpc_call_cancel is threadsafe
            AssertCallOk(call.Cancel());
		}

        public void CancelWithStatus(Status status) {
            // grpc_call_cancel_with_status is threadsafe
            AssertCallOk(call.CancelWithStatus(status));
        }

		public Status Wait()
		{
			Event ev = cq.Pluck(finished_tag, Timespec.InfFuture);
			return ev.FinishedStatus;
		}

		// blocking read...
		public byte[] Read()
		{
            lock (myLock)
            {
                AssertCallOk(call.StartRead(read_tag));
            }
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
                if (call != null) {
                    call.Dispose();
                }
            }
            finally
            {
                if (cq != null)
                {
                    cq.Dispose();
                }
            }
        }

        private static void AssertCallOk(GRPCCallError callError)
        {
            Trace.Assert(callError == GRPCCallError.GRPC_CALL_OK, "Status not GRPC_CALL_OK");
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

            public void CancelWithStatus(Status status)
            {
                parent.CancelWithStatus(status);
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