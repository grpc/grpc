#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;

namespace Grpc.Core
{
    // TODO: element names should changed to comply with C# naming conventions.
    /// <summary>
    /// based on grpc_status_code from grpc/status.h
    /// </summary>
    public enum StatusCode
    {
        /* Not an error; returned on success

     HTTP Mapping: 200 OK */
        OK = 0,
        /* The operation was cancelled (typically by the caller).

     HTTP Mapping: 499 Client Closed Request */
        Cancelled = 1,
        /* Unknown error.  An example of where this error may be returned is
     if a Status value received from another address space belongs to
     an error-space that is not known in this address space.  Also
     errors raised by APIs that do not return enough error information
     may be converted to this error.

     HTTP Mapping: 500 Internal Server Error */
        Unknown = 2,
        /* Client specified an invalid argument.  Note that this differs
     from FAILED_PRECONDITION.  INVALID_ARGUMENT indicates arguments
     that are problematic regardless of the state of the system
     (e.g., a malformed file name).

     HTTP Mapping: 400 Bad Request */
        InvalidArgument = 3,
        /* Deadline expired before operation could complete.  For operations
     that change the state of the system, this error may be returned
     even if the operation has completed successfully.  For example, a
     successful response from a server could have been delayed long
     enough for the deadline to expire.

     HTTP Mapping: 504 Gateway Timeout */
        DeadlineExceeded = 4,
        /* Some requested entity (e.g., file or directory) was not found.

     HTTP Mapping: 404 Not Found */
        NotFound = 5,
        /* Some entity that we attempted to create (e.g., file or directory)
     already exists.

     HTTP Mapping: 409 Conflict */
        AlreadyExists = 6,
        /* The caller does not have permission to execute the specified
     operation.  PERMISSION_DENIED must not be used for rejections
     caused by exhausting some resource (use RESOURCE_EXHAUSTED
     instead for those errors).  PERMISSION_DENIED must not be
     used if the caller can not be identified (use UNAUTHENTICATED
     instead for those errors).

     HTTP Mapping: 403 Forbidden */
        PermissionDenied = 7,
        /* The request does not have valid authentication credentials for the
     operation.

     HTTP Mapping: 401 Unauthorized */
        Unauthenticated = 16,
        /* Some resource has been exhausted, perhaps a per-user quota, or
     perhaps the entire file system is out of space.

     HTTP Mapping: 429 Too Many Requests */
        ResourceExhausted = 8,
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
        FailedPrecondition = 9,
        /* The operation was aborted, typically due to a concurrency issue
     like sequencer check failures, transaction aborts, etc.

     See litmus test above for deciding between FAILED_PRECONDITION,
     ABORTED, and UNAVAILABLE.

     HTTP Mapping: 409 Conflict */
        Aborted = 10,
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
        OutOfRange = 11,
        /* Operation is not implemented or not supported/enabled in this service.

     HTTP Mapping: 501 Not Implemented */
        Unimplemented = 12,
        /* Internal errors.  Means some invariants expected by underlying
     system has been broken.  If you see one of these errors,
     something is very broken.

     HTTP Mapping: 500 Internal Server Error */
        Internal = 13,
        /* The service is currently unavailable.  This is a most likely a
     transient condition and may be corrected by retrying with
     a backoff.

     See litmus test above for deciding between FAILED_PRECONDITION,
     ABORTED, and UNAVAILABLE.

     HTTP Mapping: 503 Service Unavailable */
        Unavailable = 14,
        /* Unrecoverable data loss or corruption.

     HTTP Mapping: 500 Internal Server Error */
        DataLoss = 15
    }
}

