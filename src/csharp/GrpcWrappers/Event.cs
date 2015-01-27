using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Wrappers
{
    /// <summary>
    /// Event of a completion queue.
    /// </summary>
    public class Event
    {
        readonly GRPCCompletionType completionType;
        readonly IntPtr tag;
        readonly byte[] readData;
        // only for RPCCompletionType.GRPC_READ
        readonly Status finishedStatus;
        // only for GRPCCompletionType.GRPC_FINISHED
        readonly GRPCOpError writeAcceptedSuccess;
        readonly GRPCOpError finishAcceptedSuccess;
        readonly GRPCOpError invokeAcceptedSuccess;

        /// <summary>
        /// Does not take ownership of the eventPtr.
        /// </summary>
        internal Event(EventSafeHandle eventHandle)
        {
            GRPCEvent ev = GRPCEvent.FromHandle(eventHandle);
            this.completionType = ev.type;
            this.tag = ev.tag;

            switch (this.completionType)
            {
                case GRPCCompletionType.GRPC_READ:
                    readData = ev.data.read != IntPtr.Zero ? ByteBuffer.ReadByteBuffer(ev.data.read) : null;
                    break;

                case GRPCCompletionType.GRPC_FINISHED:
				    // TODO: handle details string encoding
                    finishedStatus = new Status(ev.data.finished.status, Marshal.PtrToStringAnsi(ev.data.finished.details));
                    break;

                case GRPCCompletionType.GRPC_WRITE_ACCEPTED:
                    writeAcceptedSuccess = ev.data.write_accepted;
                    break;

                case GRPCCompletionType.GRPC_FINISH_ACCEPTED:
                    finishAcceptedSuccess = ev.data.finish_accepted;
                    break;

                case GRPCCompletionType.GRPC_INVOKE_ACCEPTED:
                    invokeAcceptedSuccess = ev.data.invoke_accepted;
                    break;


            // TODO: handling for other completion types:
            // client_metadata_read
            // server_rpc_new
            }

        }

        public GRPCCompletionType CompletionType
        {
            get
            {
                return completionType;
            }
        }

        public IntPtr Tag
        {
            get
            {
                return tag;
            }
        }

        public byte[] ReadData
        {
            get
            {
                return readData;
            }
        }

        public Status FinishedStatus
        {
            get
            {
                return finishedStatus;
            }
        }

        public GRPCOpError WriteAcceptedSuccess
        {
            get
            {
                return writeAcceptedSuccess;
            }
        }

        /// <summary>
        /// grpc_event from grpc/grpc.h
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        private struct GRPCEvent
        {
            public GRPCCompletionType type;
            public IntPtr tag;
            public IntPtr call;
            public Data data;


            /// <summary>
            /// Returns a view of event represented by event handle.
            /// If eventHandle is released, the view becomes invalid.
            /// </summary>
            public static GRPCEvent FromHandle(EventSafeHandle eventHandle)
            {
                return (GRPCEvent)Marshal.PtrToStructure(eventHandle.DangerousGetHandle(), typeof(GRPCEvent));
            }

            // to simulate union
            [StructLayout(LayoutKind.Explicit)]
            public struct Data
            {
                [FieldOffset(0)]
                public IntPtr read;
                // grpc_byte_buffer*
                [FieldOffset(0)]
                public GRPCOpError write_accepted;
                [FieldOffset(0)]
                public GRPCOpError finish_accepted;
                [FieldOffset(0)]
                public GRPCOpError invoke_accepted;
                [FieldOffset(0)]
                public FinishedData finished;
                // TODO: client_metadata_read
                // TODO: finished
                // TODO: server_rpc_new
            }

            [StructLayout(LayoutKind.Sequential)]
            public struct FinishedData
            {
                public StatusCode status;
                public IntPtr details;
                // const char*
                public UIntPtr metadataCount;
                public IntPtr metadataElements;
                // grpc_metadata*
            }
        }
    }

    public class EventSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("libgrpc.so")]
        static extern void grpc_event_finish(IntPtr ev);

        protected override bool ReleaseHandle()
        {
            grpc_event_finish(handle);
            return true;
        }
    }
}