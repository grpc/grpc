using System;
using System.Runtime.InteropServices;
using System.Diagnostics.Contracts;

namespace Google.GRPC.Interop
{
	// Wraps a native object as a disposable.
	public abstract class WrappedNative : IDisposable
	{
		private IntPtr rawPtr;
		// TODO: do we need the lock for thread safety or should the synchronization
		// of the wrapped object be left to the user?
		private object myLock = new object();

		protected WrappedNative(IntPtr rawPtr)
		{
			lock (myLock) {
				Contract.Requires (rawPtr != IntPtr.Zero);
				this.rawPtr = rawPtr;
			}
		}

		~WrappedNative() {
			Dispose(false);
		}

		public IntPtr RawPointer {
			get {
				lock (myLock) {
					Contract.Requires (rawPtr != IntPtr.Zero);
					return rawPtr;
				}
			}
		}

		// destroys the object represented by rawPtr
		protected abstract void Destroy();


		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			lock (myLock) {
				if (disposing && rawPtr != IntPtr.Zero) {
					Destroy();
				}
				rawPtr = IntPtr.Zero;
			}
		}
	}
}