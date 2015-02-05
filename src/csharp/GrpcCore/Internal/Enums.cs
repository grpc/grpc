using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Core.Internal
{
    /// <summary>
    /// from grpc/grpc.h
    /// </summary>
    internal enum GRPCCallError
    {
        /* everything went ok */
        GRPC_CALL_OK = 0,
        /* something failed, we don't know what */
        GRPC_CALL_ERROR,
        /* this method is not available on the server */
        GRPC_CALL_ERROR_NOT_ON_SERVER,
        /* this method is not available on the client */
        GRPC_CALL_ERROR_NOT_ON_CLIENT,
        /* this method must be called before server_accept */
        GRPC_CALL_ERROR_ALREADY_ACCEPTED,
        /* this method must be called before invoke */
        GRPC_CALL_ERROR_ALREADY_INVOKED,
        /* this method must be called after invoke */
        GRPC_CALL_ERROR_NOT_INVOKED,
        /* this call is already finished
     (writes_done or write_status has already been called) */
        GRPC_CALL_ERROR_ALREADY_FINISHED,
        /* there is already an outstanding read/write operation on the call */
        GRPC_CALL_ERROR_TOO_MANY_OPERATIONS,
        /* the flags value was illegal for this call */
        GRPC_CALL_ERROR_INVALID_FLAGS
    }

    /// <summary>
    /// grpc_completion_type from grpc/grpc.h
    /// </summary>
    internal enum GRPCCompletionType
    {
        GRPC_QUEUE_SHUTDOWN,
        /* Shutting down */
        GRPC_READ,
        /* A read has completed */
        GRPC_INVOKE_ACCEPTED,
        /* An invoke call has been accepted by flow
                                control */
        GRPC_WRITE_ACCEPTED,
        /* A write has been accepted by
                                flow control */
        GRPC_FINISH_ACCEPTED,
        /* writes_done or write_status has been accepted */
        GRPC_CLIENT_METADATA_READ,
        /* The metadata array sent by server received at
                                client */
        GRPC_FINISHED,
        /* An RPC has finished. The event contains status.
                                On the server this will be OK or Cancelled. */
        GRPC_SERVER_RPC_NEW,
        /* A new RPC has arrived at the server */
        GRPC_COMPLETION_DO_NOT_USE
        /* must be last, forces users to include
                                a default: case */
    }

    /// <summary>
    /// grpc_op_error from grpc/grpc.h
    /// </summary>
    internal enum GRPCOpError
    {
        /* everything went ok */
        GRPC_OP_OK = 0,
        /* something failed, we don't know what */
        GRPC_OP_ERROR
    }
}

