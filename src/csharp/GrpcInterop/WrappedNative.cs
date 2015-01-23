using System;
using System.Runtime.InteropServices;
using System.Diagnostics.Contracts;

namespace Google.GRPC.Interop
{
    /// <summary>
    /// Wraps a native object as Disposable.
    /// Destroy() method is called by Dispose() appropriately.
    /// </summary>
    public abstract class WrappedNative : IDisposable
    {
        IntPtr rawPtr;

        /// <summary>
        /// rawPtr cannot be null.
        /// </summary>
        protected WrappedNative(IntPtr rawPtr)
        {
            this.rawPtr = rawPtr;
        }

        ~WrappedNative()
        {
            Dispose(false);
        }

        public IntPtr RawPointer
        {
            get
            {
                Contract.Requires(rawPtr != IntPtr.Zero);
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
            if (rawPtr != IntPtr.Zero)
            {
                if (disposing)
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
}