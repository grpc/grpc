<?php

/**
 * @generate-class-entries
 * @generate-function-entries
 * @generate-legacy-arginfo
 */

namespace Grpc
{

  /**
   * everything went ok
   */
  const CALL_OK = 0;

  /**
   * something failed, we don't know what
   */
  const CALL_ERROR = 1;

  /**
   * this method is not available on the server
   */
  const CALL_ERROR_NOT_ON_SERVER = 2;

  /**
   * this method is not available on the client
   */
  const CALL_ERROR_NOT_ON_CLIENT = 3;

  /**
   * this method must be called before server_accept
   */
  const CALL_ERROR_ALREADY_ACCEPTED = 4;

  /**
   * this method must be called before invoke
   */
  const CALL_ERROR_ALREADY_INVOKED = 5;

  /**
   * this method must be called after invoke
   */
  const CALL_ERROR_NOT_INVOKED = 6;

  /**
   * this call is already finished
   * (writes_done or write_status has already been called)
   */
  const CALL_ERROR_ALREADY_FINISHED = 7;

  /**
   * there is already an outstanding read/write operation on the call
   */
  const CALL_ERROR_TOO_MANY_OPERATIONS = 8;

  /**
   * the flags value was illegal for this call
   */
  const CALL_ERROR_INVALID_FLAGS = 9;

  /**
   * invalid metadata was passed to this call
   */
  const CALL_ERROR_INVALID_METADATA = 10;

  /**
   * invalid message was passed to this call
   */
  const CALL_ERROR_INVALID_MESSAGE = 11;

  /**
   * completion queue for notification has not been registered with the
   * server
   */
  const CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE = 12;

  /**
   * this batch of operations leads to more operations than allowed
   */
  const CALL_ERROR_BATCH_TOO_BIG = 13;

  /**
   * payload type requested is not the type registered
   */
  const CALL_ERROR_PAYLOAD_TYPE_MISMATCH = 14;

  /*
   * Register write flags
   */

  /**
   * Hint that the write may be buffered and need not go out on the wire
   * immediately. GRPC is free to buffer the message until the next non-buffered
   * write, or until writes_done, but it need not buffer completely or at all.
   */
  const WRITE_BUFFER_HINT = 1;

  /**
   * Force compression to be disabled for a particular write
   * (start_write/add_metadata). Illegal on invoke/accept.
   */
  const WRITE_NO_COMPRESS = 2;

  /*
   * Register status constants
   */

  /**
   * Not an error; returned on success
   */
  const STATUS_OK = 0;

  /**
   * The operation was cancelled (typically by the caller).
   */
  const STATUS_CANCELLED = 1;

  /**
   * Unknown error.  An example of where this error may be returned is
   * if a Status value received from another address space belongs to
   * an error-space that is not known in this address space.  Also
   * errors raised by APIs that do not return enough error information
   * may be converted to this error.
   */
  const STATUS_UNKNOWN = 2;

  /**
   * Client specified an invalid argument.  Note that this differs
   * from FAILED_PRECONDITION.  INVALID_ARGUMENT indicates arguments
   * that are problematic regardless of the state of the system
   * (e.g., a malformed file name).
   */
  const STATUS_INVALID_ARGUMENT = 3;

  /**
   * Deadline expired before operation could complete.  For operations
   * that change the state of the system, this error may be returned
   * even if the operation has completed successfully.  For example, a
   * successful response from a server could have been delayed long
   * enough for the deadline to expire.
   */
  const STATUS_DEADLINE_EXCEEDED = 4;

  /**
   * Some requested entity (e.g., file or directory) was not found.
   */
  const STATUS_NOT_FOUND = 5;

  /* Some entity that we attempted to create (e.g., file or directory)
   * already exists.
   */
  const STATUS_ALREADY_EXISTS = 6;

  /**
   * The caller does not have permission to execute the specified
   * operation.  PERMISSION_DENIED must not be used for rejections
   * caused by exhausting some resource (use RESOURCE_EXHAUSTED
   * instead for those errors).  PERMISSION_DENIED must not be
   * used if the caller can not be identified (use UNAUTHENTICATED
   * instead for those errors).
   */
  const STATUS_PERMISSION_DENIED = 7;

  /**
   * The request does not have valid authentication credentials for the
   * operation.
   */
  const STATUS_UNAUTHENTICATED = 16;

  /**
   * Some resource has been exhausted, perhaps a per-user quota, or
   * perhaps the entire file system is out of space.
   */
  const STATUS_RESOURCE_EXHAUSTED = 8;

  /**
   * Operation was rejected because the system is not in a state
   * required for the operation's execution.  For example, directory
   * to be deleted may be non-empty, an rmdir operation is applied to
   * a non-directory, etc.
   *
   * A litmus test that may help a service implementor in deciding
   * between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
   *  (a) Use UNAVAILABLE if the client can retry just the failing call.
   *  (b) Use ABORTED if the client should retry at a higher-level
   *      (e.g., restarting a read-modify-write sequence).
   *  (c) Use FAILED_PRECONDITION if the client should not retry until
   *      the system state has been explicitly fixed.  E.g., if an "rmdir"
   *      fails because the directory is non-empty, FAILED_PRECONDITION
   *      should be returned since the client should not retry unless
   *      they have first fixed up the directory by deleting files from it.
   *  (d) Use FAILED_PRECONDITION if the client performs conditional
   *      REST Get/Update/Delete on a resource and the resource on the
   *      server does not match the condition. E.g., conflicting
   *      read-modify-write on the same resource.
   */
  const STATUS_FAILED_PRECONDITION = 9;

  /**
   * The operation was aborted, typically due to a concurrency issue
   * like sequencer check failures, transaction aborts, etc.
   *
   * See litmus test above for deciding between FAILED_PRECONDITION,
   * ABORTED, and UNAVAILABLE.
   */
  const STATUS_ABORTED = 10;

  /**
   * Operation was attempted past the valid range.  E.g., seeking or
   * reading past end of file.
   *
   * Unlike INVALID_ARGUMENT, this error indicates a problem that may
   * be fixed if the system state changes. For example, a 32-bit file
   * system will generate INVALID_ARGUMENT if asked to read at an
   * offset that is not in the range [0,2^32-1], but it will generate
   * OUT_OF_RANGE if asked to read from an offset past the current
   * file size.
   *
   * There is a fair bit of overlap between FAILED_PRECONDITION and
   * OUT_OF_RANGE.  We recommend using OUT_OF_RANGE (the more specific
   * error) when it applies so that callers who are iterating through
   * a space can easily look for an OUT_OF_RANGE error to detect when
   * they are done.
   */
  const STATUS_OUT_OF_RANGE = 11;

  /**
   * Operation is not implemented or not supported/enabled in this service.
   */
  const STATUS_UNIMPLEMENTED = 12;

  /**
   * Internal errors.  Means some invariants expected by underlying
   *  system has been broken.  If you see one of these errors,
   *  something is very broken.
   */
  const STATUS_INTERNAL = 13;

  /**
   * The service is currently unavailable.  This is a most likely a
   * transient condition and may be corrected by retrying with
   * a backoff.
   *
   * See litmus test above for deciding between FAILED_PRECONDITION,
   * ABORTED, and UNAVAILABLE.
   */
  const STATUS_UNAVAILABLE = 14;

  /**
   * Unrecoverable data loss or corruption.
   */
  const STATUS_DATA_LOSS = 15;

  /*
   * Register op type constants
   */

  /**
   * Send initial metadata: one and only one instance MUST be sent for each
   * call, unless the call was cancelled - in which case this can be skipped.
   * This op completes after all bytes of metadata have been accepted by
   * outgoing flow control.
   */
  const OP_SEND_INITIAL_METADATA = 0;

  /**
   * Send a message: 0 or more of these operations can occur for each call.
   * This op completes after all bytes for the message have been accepted by
   * outgoing flow control.
   */
  const OP_SEND_MESSAGE = 1;

  /** Send a close from the client: one and only one instance MUST be sent from
   * the client, unless the call was cancelled - in which case this can be
   * skipped.
   * This op completes after all bytes for the call (including the close)
   * have passed outgoing flow control.
   */
  const OP_SEND_CLOSE_FROM_CLIENT = 2;

  /**
   * Send status from the server: one and only one instance MUST be sent from
   * the server unless the call was cancelled - in which case this can be
   * skipped.
   * This op completes after all bytes for the call (including the status)
   * have passed outgoing flow control.
   */
  const OP_SEND_STATUS_FROM_SERVER = 3;

  /**
   * Receive initial metadata: one and only one MUST be made on the client,
   * must not be made on the server.
   * This op completes after all initial metadata has been read from the
   * peer.
   */
  const OP_RECV_INITIAL_METADATA = 4;

  /**
   * Receive a message: 0 or more of these operations can occur for each call.
   * This op completes after all bytes of the received message have been
   * read, or after a half-close has been received on this call.
   */
  const OP_RECV_MESSAGE = 5;

  /**
   * Receive status on the client: one and only one must be made on the client.
   * This operation always succeeds, meaning ops paired with this operation
   * will also appear to succeed, even though they may not have. In that case
   * the status will indicate some failure.
   * This op completes after all activity on the call has completed.
   */
  const OP_RECV_STATUS_ON_CLIENT = 6;

  /**
   * Receive close on the server: one and only one must be made on the
   * server.
   * This op completes after the close has been received by the server.
   * This operation always succeeds, meaning ops paired with this operation
   * will also appear to succeed, even though they may not have.
   */
  const OP_RECV_CLOSE_ON_SERVER = 7;

  /*
   * Register connectivity state constants
   */

  /**
   * channel is idle
   */
  const CHANNEL_IDLE = 0;

  /**
   * channel is connecting
   */
  const CHANNEL_CONNECTING = 1;

  /**
   * channel is ready for work
   */
  const CHANNEL_READY = 2;

  /**
   * channel has seen a failure but expects to recover
   */
  const CHANNEL_TRANSIENT_FAILURE = 3;

  /**
   * channel has seen a failure that it cannot recover from
   */
  const CHANNEL_SHUTDOWN      = 4;
  const CHANNEL_FATAL_FAILURE = 4;
}
