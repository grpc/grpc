using System;
using System.Runtime.InteropServices;
using System.Diagnostics.Contracts;

namespace Google.GRPC.Interop
{
    /// <summary>
    /// Wraps a native resource and provides a standardized way how to dispose it.
    /// Destroy() method is called by Dispose() appropriately.
    /// </summary>
    public abstract class WrappedNative : IDisposable
    {
        IntPtr rawPtr = IntPtr.Zero;

        /// <summary>
        /// Runs the delegate to allocate resource.
        /// </summary>
        protected WrappedNative(Func<IntPtr> allocDelegate)
        {
            this.rawPtr = allocDelegate();
        }

        ~WrappedNative()
        {
            Dispose(false);
        }

        public IntPtr RawPointer
        {
            get
            {
                return rawPtr;
            }
        }

        /// <summary>
        /// Destroys the object represented by rawPtr.
        /// </summary>
        protected abstract void Destroy();

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (disposing && rawPtr != IntPtr.Zero)
            {
                try
                {
                    Destroy();
                }
                finally
                {
                    rawPtr = IntPtr.Zero;
                }
            }
        }
    }
}