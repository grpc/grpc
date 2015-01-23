using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
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

        public Event(GRPCEvent ev)
        {
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
    }
}