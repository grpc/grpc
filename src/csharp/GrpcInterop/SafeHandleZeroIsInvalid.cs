using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
    /// <summary>
    /// Safe handle to wrap native objects.
    /// </summary>
    public abstract class SafeHandleZeroIsInvalid : SafeHandle
    {
        public SafeHandleZeroIsInvalid() : base(IntPtr.Zero, true)
        {
        }

        public SafeHandleZeroIsInvalid(bool ownsHandle) : base(IntPtr.Zero, ownsHandle)
        {
        }

        public override bool IsInvalid
        {
            get
            {
                return handle == IntPtr.Zero;
            }
        }
    }
}

