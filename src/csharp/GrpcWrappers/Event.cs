using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// grpc_event from grpc/grpc.h
    /// </summary>
    internal class EventSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("libgrpc.so")]
        static extern void grpc_event_finish(IntPtr ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCompletionType grpc_event_get_type(EventSafeHandle ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCOpError grpc_event_invoke_accepted(EventSafeHandle ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCOpError grpc_event_write_accepted(EventSafeHandle ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCOpError grpc_event_finish_accepted(EventSafeHandle ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern StatusCode grpc_event_finished_status(EventSafeHandle ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpc_event_finished_details(EventSafeHandle ev);  // returns const char*

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpc_event_read_length(EventSafeHandle ev);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpc_event_read_copy_to_buffer(EventSafeHandle ev, byte[] buffer, UIntPtr bufferLen);

        public GRPCCompletionType GetCompletionType()
        {
            return grpc_event_get_type(this);
        }

        public GRPCOpError GetInvokeAccepted()
        {
            return grpc_event_invoke_accepted(this);
        }

        public GRPCOpError GetWriteAccepted()
        {
            return grpc_event_write_accepted(this);
        }

        public GRPCOpError GetFinishAccepted()
        {
            return grpc_event_finish_accepted(this);
        }

        public Status GetFinished()
        {
            String details = Marshal.PtrToStringAnsi(grpc_event_finished_details(this));
            return new Status(grpc_event_finished_status(this), details);
        }

        public byte[] GetReadData()
        {
            IntPtr len = grpc_event_read_length(this);
            if (len == new IntPtr(-1))
            {
                return null;
            }
            byte[] data = new byte[(int) len];
            grpc_event_read_copy_to_buffer(this, data, new UIntPtr((ulong)data.Length));
            return data;
        }

        protected override bool ReleaseHandle()
        {
            grpc_event_finish(handle);
            return true;
        }
    }
}