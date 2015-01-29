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
		static readonly IntPtr MetadataReadTag = new IntPtr(1);
		static readonly IntPtr FinishedTag = new IntPtr(2);
		static readonly IntPtr WriteTag = new IntPtr(3);
		static readonly IntPtr HalfcloseTag = new IntPtr(4);
		static readonly IntPtr ReadTag = new IntPtr(5);

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
                call.Invoke(cq, MetadataReadTag, FinishedTag, buffered);
            }
		}

		// blocking write...
		public bool Write(byte[] payload)
		{
            lock (myLock)
            {
                call.StartWrite(payload, WriteTag, false);
            }
            using (EventSafeHandle ev = cq.Pluck(WriteTag, Timespec.InfFuture))
            {
                return (ev.GetWriteAccepted() == GRPCOpError.GRPC_OP_OK);
            }
		}

		public void WritesDone()
		{
            lock (myLock)
            {
                call.WritesDone(HalfcloseTag);
            }

            // TODO: check that event was successful...
            using (EventSafeHandle ev = cq.Pluck(HalfcloseTag, Timespec.InfFuture))
            {
            }
		}

		public void Cancel()
		{
            // grpc_call_cancel is threadsafe
            call.Cancel();
		}

        public void CancelWithStatus(Status status) {
            // grpc_call_cancel_with_status is threadsafe
            call.CancelWithStatus(status);
        }

		public Status Wait()
        { 
            using (EventSafeHandle ev = cq.Pluck(FinishedTag, Timespec.InfFuture))
            {
                return ev.GetFinished();
            }
		}

		// blocking read...
		public byte[] Read()
		{
            lock (myLock)
            {
                call.StartRead(ReadTag);
            }
            using (EventSafeHandle ev = cq.Pluck(ReadTag, Timespec.InfFuture))
            {
                return ev.GetReadData();
            }
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