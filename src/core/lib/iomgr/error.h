/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_CORE_LIB_IOMGR_ERROR_H
#define GRPC_CORE_LIB_IOMGR_ERROR_H

#include <stdbool.h>
#include <stdint.h>

#include <grpc/status.h>
#include <grpc/support/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque representation of an error.
/// Errors are refcounted objects that represent the result of an operation.
/// Ownership laws:
///  if a grpc_error is returned by a function, the caller owns a ref to that
///    instance
///  if a grpc_error is passed to a grpc_closure callback function (functions
///    with the signature:
///      void (*f)(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error))
///    then those functions do not own a ref to error (but are free to manually
///    take a reference).
///  if a grpc_error is passed to *ANY OTHER FUNCTION* then that function takes
///    ownership of the error
/// Errors have:
///  a set of ints, strings, and timestamps that describe the error
///  always present are:
///    GRPC_ERROR_STR_FILE, GRPC_ERROR_INT_FILE_LINE - source location the error
///      was generated
///    GRPC_ERROR_STR_DESCRIPTION - a human readable description of the error
///    GRPC_ERROR_TIME_CREATED - a timestamp indicating when the error happened
///  an error can also have children; these are other errors that are believed
///    to have contributed to this one. By accumulating children, we can begin
///    to root cause high level failures from low level failures, without having
///    to derive execution paths from log lines
typedef struct grpc_error grpc_error;

typedef enum {
  /// 'errno' from the operating system
  GRPC_ERROR_INT_ERRNO,
  /// __LINE__ from the call site creating the error
  GRPC_ERROR_INT_FILE_LINE,
  /// stream identifier: for errors that are associated with an individual
  /// wire stream
  GRPC_ERROR_INT_STREAM_ID,
  /// grpc status code representing this error
  GRPC_ERROR_INT_GRPC_STATUS,
  /// offset into some binary blob (usually represented by
  /// GRPC_ERROR_STR_RAW_BYTES) where the error occurred
  GRPC_ERROR_INT_OFFSET,
  /// context sensitive index associated with the error
  GRPC_ERROR_INT_INDEX,
  /// context sensitive size associated with the error
  GRPC_ERROR_INT_SIZE,
  /// http2 error code associated with the error (see the HTTP2 RFC)
  GRPC_ERROR_INT_HTTP2_ERROR,
  /// TSI status code associated with the error
  GRPC_ERROR_INT_TSI_CODE,
  /// grpc_security_status associated with the error
  GRPC_ERROR_INT_SECURITY_STATUS,
  /// WSAGetLastError() reported when this error occurred
  GRPC_ERROR_INT_WSA_ERROR,
  /// File descriptor associated with this error
  GRPC_ERROR_INT_FD,
  /// HTTP status (i.e. 404)
  GRPC_ERROR_INT_HTTP_STATUS,
  /// context sensitive limit associated with the error
  GRPC_ERROR_INT_LIMIT,
  /// chttp2: did the error occur while a write was in progress
  GRPC_ERROR_INT_OCCURRED_DURING_WRITE,
} grpc_error_ints;

typedef enum {
  /// top-level textual description of this error
  GRPC_ERROR_STR_DESCRIPTION,
  /// source file in which this error occurred
  GRPC_ERROR_STR_FILE,
  /// operating system description of this error
  GRPC_ERROR_STR_OS_ERROR,
  /// syscall that generated this error
  GRPC_ERROR_STR_SYSCALL,
  /// peer that we were trying to communicate when this error occurred
  GRPC_ERROR_STR_TARGET_ADDRESS,
  /// grpc status message associated with this error
  GRPC_ERROR_STR_GRPC_MESSAGE,
  /// hex dump (or similar) with the data that generated this error
  GRPC_ERROR_STR_RAW_BYTES,
  /// tsi error string associated with this error
  GRPC_ERROR_STR_TSI_ERROR,
  /// filename that we were trying to read/write when this error occurred
  GRPC_ERROR_STR_FILENAME,
  /// which data was queued for writing when the error occurred
  GRPC_ERROR_STR_QUEUED_BUFFERS
} grpc_error_strs;

typedef enum {
  /// timestamp of error creation
  GRPC_ERROR_TIME_CREATED,
} grpc_error_times;

/// The following "special" errors can be propagated without allocating memory.
/// They are always even so that other code (particularly combiner locks) can
/// safely use the lower bit for themselves.

#define GRPC_ERROR_NONE ((grpc_error *)NULL)
#define GRPC_ERROR_OOM ((grpc_error *)2)
#define GRPC_ERROR_CANCELLED ((grpc_error *)4)

const char *grpc_error_string(grpc_error *error);
void grpc_error_free_string(const char *str);

/// Create an error - but use GRPC_ERROR_CREATE instead
grpc_error *grpc_error_create(const char *file, int line, const char *desc,
                              grpc_error **referencing, size_t num_referencing);
/// Create an error (this is the preferred way of generating an error that is
///   not due to a system call - for system calls, use GRPC_OS_ERROR or
///   GRPC_WSA_ERROR as appropriate)
/// \a referencing is an array of num_referencing elements indicating one or
/// more errors that are believed to have contributed to this one
/// err = grpc_error_create(x, y, z, r, nr) is equivalent to:
///   err = grpc_error_create(x, y, z, NULL, 0);
///   for (i=0; i<nr; i++) err = grpc_error_add_child(err, r[i]);
#define GRPC_ERROR_CREATE(desc) \
  grpc_error_create(__FILE__, __LINE__, desc, NULL, 0)

// Create an error that references some other errors. This function adds a
// reference to each error in errs - it does not consume an existing reference
#define GRPC_ERROR_CREATE_REFERENCING(desc, errs, count) \
  grpc_error_create(__FILE__, __LINE__, desc, errs, count)

//#define GRPC_ERROR_REFCOUNT_DEBUG
#ifdef GRPC_ERROR_REFCOUNT_DEBUG
grpc_error *grpc_error_ref(grpc_error *err, const char *file, int line,
                           const char *func);
void grpc_error_unref(grpc_error *err, const char *file, int line,
                      const char *func);
#define GRPC_ERROR_REF(err) grpc_error_ref(err, __FILE__, __LINE__, __func__)
#define GRPC_ERROR_UNREF(err) \
  grpc_error_unref(err, __FILE__, __LINE__, __func__)
#else
grpc_error *grpc_error_ref(grpc_error *err);
void grpc_error_unref(grpc_error *err);
#define GRPC_ERROR_REF(err) grpc_error_ref(err)
#define GRPC_ERROR_UNREF(err) grpc_error_unref(err)
#endif

grpc_error *grpc_error_set_int(grpc_error *src, grpc_error_ints which,
                               intptr_t value) GRPC_MUST_USE_RESULT;
bool grpc_error_get_int(grpc_error *error, grpc_error_ints which, intptr_t *p);
grpc_error *grpc_error_set_time(grpc_error *src, grpc_error_times which,
                                gpr_timespec value) GRPC_MUST_USE_RESULT;
grpc_error *grpc_error_set_str(grpc_error *src, grpc_error_strs which,
                               const char *value) GRPC_MUST_USE_RESULT;
/// Returns NULL if the specified string is not set.
/// Caller does NOT own return value.
const char *grpc_error_get_str(grpc_error *error, grpc_error_strs which);

/// A utility function to get the status code and message to be returned
/// to the application.  If not set in the top-level message, looks
/// through child errors until it finds the first one with these attributes.
void grpc_error_get_status(grpc_error *error, grpc_status_code *code,
                           const char **msg);

/// Add a child error: an error that is believed to have contributed to this
/// error occurring. Allows root causing high level errors from lower level
/// errors that contributed to them.
grpc_error *grpc_error_add_child(grpc_error *src,
                                 grpc_error *child) GRPC_MUST_USE_RESULT;
grpc_error *grpc_os_error(const char *file, int line, int err,
                          const char *call_name) GRPC_MUST_USE_RESULT;
/// create an error associated with errno!=0 (an 'operating system' error)
#define GRPC_OS_ERROR(err, call_name) \
  grpc_os_error(__FILE__, __LINE__, err, call_name)
grpc_error *grpc_wsa_error(const char *file, int line, int err,
                           const char *call_name) GRPC_MUST_USE_RESULT;
/// windows only: create an error associated with WSAGetLastError()!=0
#define GRPC_WSA_ERROR(err, call_name) \
  grpc_wsa_error(__FILE__, __LINE__, err, call_name)

bool grpc_log_if_error(const char *what, grpc_error *error, const char *file,
                       int line);
#define GRPC_LOG_IF_ERROR(what, error) \
  grpc_log_if_error((what), (error), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_IOMGR_ERROR_H */
