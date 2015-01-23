using System;
using System.Runtime.InteropServices;

namespace Google.GRPC.Interop
{
    public static class GRPCUtils
    {
        [DllImport("libgrpc.so")]
        static extern void grpc_init();

        [DllImport("libgrpc.so")]
        static extern void grpc_shutdown();

        public static void Init()
        {
            grpc_init();
            Console.WriteLine("GRPC initialized.");
        }

        public static void Shutdown()
        {
            grpc_shutdown();
            Console.WriteLine("GRPC shutdown.");
        }

        public const UInt32 GRPC_WRITE_BUFFER_HINT = 1;
    }

    public enum GRPCCallError
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

    public enum GRPCCompletionType
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

    public enum GRPCOpError
    {
        /* everything went ok */
        GRPC_OP_OK = 0,
        /* something failed, we don't know what */
        GRPC_OP_ERROR
    }

    public enum GRPCStatusCode
    {
        /* Not an error; returned on success

     HTTP Mapping: 200 OK */
        GRPC_STATUS_OK = 0,
        /* The operation was cancelled (typically by the caller).

     HTTP Mapping: 499 Client Closed Request */
        GRPC_STATUS_CANCELLED = 1,
        /* Unknown error.  An example of where this error may be returned is
     if a Status value received from another address space belongs to
     an error-space that is not known in this address space.  Also
     errors raised by APIs that do not return enough error information
     may be converted to this error.

     HTTP Mapping: 500 Internal Server Error */
        GRPC_STATUS_UNKNOWN = 2,
        /* Client specified an invalid argument.  Note that this differs
     from FAILED_PRECONDITION.  INVALID_ARGUMENT indicates arguments
     that are problematic regardless of the state of the system
     (e.g., a malformed file name).

     HTTP Mapping: 400 Bad Request */
        GRPC_STATUS_INVALID_ARGUMENT = 3,
        /* Deadline expired before operation could complete.  For operations
     that change the state of the system, this error may be returned
     even if the operation has completed successfully.  For example, a
     successful response from a server could have been delayed long
     enough for the deadline to expire.

     HTTP Mapping: 504 Gateway Timeout */
        GRPC_STATUS_DEADLINE_EXCEEDED = 4,
        /* Some requested entity (e.g., file or directory) was not found.

     HTTP Mapping: 404 Not Found */
        GRPC_STATUS_NOT_FOUND = 5,
        /* Some entity that we attempted to create (e.g., file or directory)
     already exists.

     HTTP Mapping: 409 Conflict */
        GRPC_STATUS_ALREADY_EXISTS = 6,
        /* The caller does not have permission to execute the specified
     operation.  PERMISSION_DENIED must not be used for rejections
     caused by exhausting some resource (use RESOURCE_EXHAUSTED
     instead for those errors).  PERMISSION_DENIED must not be
     used if the caller can not be identified (use UNAUTHENTICATED
     instead for those errors).

     HTTP Mapping: 403 Forbidden */
        GRPC_STATUS_PERMISSION_DENIED = 7,
        /* The request does not have valid authentication credentials for the
     operation.

     HTTP Mapping: 401 Unauthorized */
        GRPC_STATUS_UNAUTHENTICATED = 16,
        /* Some resource has been exhausted, perhaps a per-user quota, or
     perhaps the entire file system is out of space.

     HTTP Mapping: 429 Too Many Requests */
        GRPC_STATUS_RESOURCE_EXHAUSTED = 8,
        /* Operation was rejected because the system is not in a state
     required for the operation's execution.  For example, directory
     to be deleted may be non-empty, an rmdir operation is applied to
     a non-directory, etc.

     A litmus test that may help a service implementor in deciding
     between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
      (a) Use UNAVAILABLE if the client can retry just the failing call.
      (b) Use ABORTED if the client should retry at a higher-level
          (e.g., restarting a read-modify-write sequence).
      (c) Use FAILED_PRECONDITION if the client should not retry until
          the system state has been explicitly fixed.  E.g., if an "rmdir"
          fails because the directory is non-empty, FAILED_PRECONDITION
          should be returned since the client should not retry unless
          they have first fixed up the directory by deleting files from it.
      (d) Use FAILED_PRECONDITION if the client performs conditional
          REST Get/Update/Delete on a resource and the resource on the
          server does not match the condition. E.g., conflicting
          read-modify-write on the same resource.

     HTTP Mapping: 400 Bad Request

     NOTE: HTTP spec says 412 Precondition Failed should only be used if
     the request contains Etag related headers. So if the server does see
     Etag related headers in the request, it may choose to return 412
     instead of 400 for this error code. */
        GRPC_STATUS_FAILED_PRECONDITION = 9,
        /* The operation was aborted, typically due to a concurrency issue
     like sequencer check failures, transaction aborts, etc.

     See litmus test above for deciding between FAILED_PRECONDITION,
     ABORTED, and UNAVAILABLE.

     HTTP Mapping: 409 Conflict */
        GRPC_STATUS_ABORTED = 10,
        /* Operation was attempted past the valid range.  E.g., seeking or
     reading past end of file.

     Unlike INVALID_ARGUMENT, this error indicates a problem that may
     be fixed if the system state changes. For example, a 32-bit file
     system will generate INVALID_ARGUMENT if asked to read at an
     offset that is not in the range [0,2^32-1], but it will generate
     OUT_OF_RANGE if asked to read from an offset past the current
     file size.

     There is a fair bit of overlap between FAILED_PRECONDITION and
     OUT_OF_RANGE.  We recommend using OUT_OF_RANGE (the more specific
     error) when it applies so that callers who are iterating through
     a space can easily look for an OUT_OF_RANGE error to detect when
     they are done.

     HTTP Mapping: 400 Bad Request */
        GRPC_STATUS_OUT_OF_RANGE = 11,
        /* Operation is not implemented or not supported/enabled in this service.

     HTTP Mapping: 501 Not Implemented */
        GRPC_STATUS_UNIMPLEMENTED = 12,
        /* Internal errors.  Means some invariants expected by underlying
     system has been broken.  If you see one of these errors,
     something is very broken.

     HTTP Mapping: 500 Internal Server Error */
        GRPC_STATUS_INTERNAL = 13,
        /* The service is currently unavailable.  This is a most likely a
     transient condition and may be corrected by retrying with
     a backoff.

     See litmus test above for deciding between FAILED_PRECONDITION,
     ABORTED, and UNAVAILABLE.

     HTTP Mapping: 503 Service Unavailable */
        GRPC_STATUS_UNAVAILABLE = 14,
        /* Unrecoverable data loss or corruption.

     HTTP Mapping: 500 Internal Server Error */
        GRPC_STATUS_DATA_LOSS = 15,
        /* Force users to include a default branch: */
        GRPC_STATUS__DO_NOT_USE = -1
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct GRPCEvent
    {
        public GRPCCompletionType type;
        public IntPtr tag;
        public IntPtr call;
        public Data data;

        public static GRPCEvent FromIntPtr(IntPtr ptr)
        {
            return (GRPCEvent)Marshal.PtrToStructure(ptr, typeof(GRPCEvent));
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
            public GRPCStatusCode status;
            public IntPtr details;
            // const char*
            public UIntPtr metadataCount;
            public IntPtr metadataElements;
            // grpc_metadata*
        }
    }
}

