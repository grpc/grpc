using System;
using System.Runtime.InteropServices;
using System.Diagnostics.Contracts;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// Wraps a native resource referenced by a SafeHandle.
    /// </summary>
    public abstract class WrappedNativeObject<T> : IDisposable
        where T : SafeHandle
    {
        protected readonly T handle;

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