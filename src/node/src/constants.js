/**
 * @license
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* The comments about status codes are copied verbatim (with some formatting
 * modifications) from include/grpc/impl/codegen/status.h, for the purpose of
 * including them in generated documentation.
 */
/**
 * Enum of status codes that gRPC can return
 * @memberof grpc
 * @alias grpc.status
 * @readonly
 * @enum {number}
 */
exports.status = {
  /** Not an error; returned on success */
  OK: 0,
  /** The operation was cancelled (typically by the caller). */
  CANCELLED: 1,
  /**
   * Unknown error.  An example of where this error may be returned is
   * if a status value received from another address space belongs to
   * an error-space that is not known in this address space.  Also
   * errors raised by APIs that do not return enough error information
   * may be converted to this error.
   */
  UNKNOWN: 2,
  /**
   * Client specified an invalid argument.  Note that this differs
   * from FAILED_PRECONDITION.  INVALID_ARGUMENT indicates arguments
   * that are problematic regardless of the state of the system
   * (e.g., a malformed file name).
   */
  INVALID_ARGUMENT: 3,
  /**
   * Deadline expired before operation could complete.  For operations
   * that change the state of the system, this error may be returned
   * even if the operation has completed successfully.  For example, a
   * successful response from a server could have been delayed long
   * enough for the deadline to expire.
   */
  DEADLINE_EXCEEDED: 4,
  /** Some requested entity (e.g., file or directory) was not found. */
  NOT_FOUND: 5,
  /**
   * Some entity that we attempted to create (e.g., file or directory)
   * already exists.
   */
  ALREADY_EXISTS: 6,
  /**
   * The caller does not have permission to execute the specified
   * operation.  PERMISSION_DENIED must not be used for rejections
   * caused by exhausting some resource (use RESOURCE_EXHAUSTED
   * instead for those errors).  PERMISSION_DENIED must not be
   * used if the caller can not be identified (use UNAUTHENTICATED
   * instead for those errors).
   */
  PERMISSION_DENIED: 7,
  /**
   * Some resource has been exhausted, perhaps a per-user quota, or
   * perhaps the entire file system is out of space.
   */
  RESOURCE_EXHAUSTED: 8,
  /**
   * Operation was rejected because the system is not in a state
   * required for the operation's execution.  For example, directory
   * to be deleted may be non-empty, an rmdir operation is applied to
   * a non-directory, etc.
   *
   * A litmus test that may help a service implementor in deciding
   * between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
   *
   *  - Use UNAVAILABLE if the client can retry just the failing call.
   *  - Use ABORTED if the client should retry at a higher-level
   *    (e.g., restarting a read-modify-write sequence).
   *  - Use FAILED_PRECONDITION if the client should not retry until
   *    the system state has been explicitly fixed.  E.g., if an "rmdir"
   *    fails because the directory is non-empty, FAILED_PRECONDITION
   *    should be returned since the client should not retry unless
   *    they have first fixed up the directory by deleting files from it.
   *  - Use FAILED_PRECONDITION if the client performs conditional
   *    REST Get/Update/Delete on a resource and the resource on the
   *    server does not match the condition. E.g., conflicting
   *    read-modify-write on the same resource.
   */
  FAILED_PRECONDITION: 9,
  /**
   * The operation was aborted, typically due to a concurrency issue
   * like sequencer check failures, transaction aborts, etc.
   *
   * See litmus test above for deciding between FAILED_PRECONDITION,
   * ABORTED, and UNAVAILABLE.
   */
  ABORTED: 10,
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
  OUT_OF_RANGE: 11,
  /** Operation is not implemented or not supported/enabled in this service. */
  UNIMPLEMENTED: 12,
  /**
   * Internal errors.  Means some invariants expected by underlying
   * system has been broken.  If you see one of these errors,
   * something is very broken.
   */
  INTERNAL: 13,
  /**
   * The service is currently unavailable.  This is a most likely a
   * transient condition and may be corrected by retrying with
   * a backoff.
   *
   * See litmus test above for deciding between FAILED_PRECONDITION,
   * ABORTED, and UNAVAILABLE. */
  UNAVAILABLE: 14,
  /** Unrecoverable data loss or corruption. */
  DATA_LOSS: 15,
  /**
   * The request does not have valid authentication credentials for the
   * operation.
   */
  UNAUTHENTICATED: 16
};

/* The comments about propagation bit flags are copied rom
 * include/grpc/impl/codegen/propagation_bits.h for the purpose of including
 * them in generated documentation.
 */
/**
 * Propagation flags: these can be bitwise or-ed to form the propagation option
 * for calls.
 *
 * Users are encouraged to write propagation masks as deltas from the default.
 * i.e. write `grpc.propagate.DEFAULTS & ~grpc.propagate.DEADLINE` to disable
 * deadline propagation.
 * @memberof grpc
 * @alias grpc.propagate
 * @enum {number}
 */
exports.propagate = {
  DEADLINE: 1,
  CENSUS_STATS_CONTEXT: 2,
  CENSUS_TRACING_CONTEXT: 4,
  CANCELLATION: 8,
  DEFAULTS: 65535
};

/* Many of the following comments are copied from
 * include/grpc/impl/codegen/grpc_types.h
 */
/**
 * Call error constants. Call errors almost always indicate bugs in the gRPC
 * library, and these error codes are mainly useful for finding those bugs.
 * @memberof grpc
 * @readonly
 * @enum {number}
 */
const callError = {
  OK: 0,
  ERROR: 1,
  NOT_ON_SERVER: 2,
  NOT_ON_CLIENT: 3,
  ALREADY_INVOKED: 5,
  NOT_INVOKED: 6,
  ALREADY_FINISHED: 7,
  TOO_MANY_OPERATIONS: 8,
  INVALID_FLAGS: 9,
  INVALID_METADATA: 10,
  INVALID_MESSAGE: 11,
  NOT_SERVER_COMPLETION_QUEUE: 12,
  BATCH_TOO_BIG: 13,
  PAYLOAD_TYPE_MISMATCH: 14
};

exports.callError = callError;

/**
 * Write flags: these can be bitwise or-ed to form write options that modify
 * how data is written.
 * @memberof grpc
 * @alias grpc.writeFlags
 * @readonly
 * @enum {number}
 */
exports.writeFlags = {
  /**
   * Hint that the write may be buffered and need not go out on the wire
   * immediately. GRPC is free to buffer the message until the next non-buffered
   * write, or until writes_done, but it need not buffer completely or at all.
   */
  BUFFER_HINT: 1,
  /**
   * Force compression to be disabled for a particular write
   */
  NO_COMPRESS: 2
};

/**
 * @memberof grpc
 * @alias grpc.logVerbosity
 * @readonly
 * @enum {number}
 */
exports.logVerbosity = {
  DEBUG: 0,
  INFO: 1,
  ERROR: 2
};
