using System;
using System.Runtime.InteropServices;
using System.Diagnostics.Contracts;

namespace Google.GRPC.Interop
{
   
    // use safehandle instead....

    /// <summary>
    /// Wraps a native resource and provides a standardized way how to dispose it.
    /// Destroy() method is called by Dispose() appropriately.
    /// </summary>
    public abstract class WrappedNativeObject<T> : IDisposable
        where T : SafeHandle
    {
        protected readonly T handle;

        /// <summary>
        /// Runs the delegate to allocate resource.
        /// </summary>
        protected WrappedNativeObject(T handle)
        {
            this.handle = handle;
        }

        public T Handle
        {
            get
            {
                return handle;
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (handle != null && !handle.IsInvalid)
            {
                handle.Dispose();
            }
        }
    }
}