/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_LIB_IOMGR_ERROR_H
#define GRPC_CORE_LIB_IOMGR_ERROR_H

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <stdbool.h>

#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/status_helper.h"

#include "absl/status/status.h"

/// Opaque representation of an error.
/// See https://github.com/grpc/grpc/blob/master/doc/core/grpc-error.md for a
/// full write up of this object.

#ifdef GRPC_ERROR_IS_ABSEIL_STATUS

typedef absl::Status grpc_error_handle;

#else  // GRPC_ERROR_IS_ABSEIL_STATUS

typedef struct grpc_error grpc_error;
typedef grpc_error* grpc_error_handle;

#endif  // GRPC_ERROR_IS_ABSEIL_STATUS

typedef enum {
  /// 'errno' from the operating system
  GRPC_ERROR_INT_ERRNO =
      static_cast<int>(grpc_core::StatusIntProperty::kErrorNo),
  /// __LINE__ from the call site creating the error
  GRPC_ERROR_INT_FILE_LINE =
      static_cast<int>(grpc_core::StatusIntProperty::kFileLine),
  /// stream identifier: for errors that are associated with an individual
  /// wire stream
  GRPC_ERROR_INT_STREAM_ID =
      static_cast<int>(grpc_core::StatusIntProperty::kStreamId),
  /// grpc status code representing this error
  GRPC_ERROR_INT_GRPC_STATUS =
      static_cast<int>(grpc_core::StatusIntProperty::kRpcStatus),
  /// offset into some binary blob (usually represented by
  /// GRPC_ERROR_STR_RAW_BYTES) where the error occurred
  GRPC_ERROR_INT_OFFSET =
      static_cast<int>(grpc_core::StatusIntProperty::kOffset),
  /// context sensitive index associated with the error
  GRPC_ERROR_INT_INDEX = static_cast<int>(grpc_core::StatusIntProperty::kIndex),
  /// context sensitive size associated with the error
  GRPC_ERROR_INT_SIZE = static_cast<int>(grpc_core::StatusIntProperty::kSize),
  /// http2 error code associated with the error (see the HTTP2 RFC)
  GRPC_ERROR_INT_HTTP2_ERROR =
      static_cast<int>(grpc_core::StatusIntProperty::kHttp2Error),
  /// TSI status code associated with the error
  GRPC_ERROR_INT_TSI_CODE =
      static_cast<int>(grpc_core::StatusIntProperty::kTsiCode),
  /// WSAGetLastError() reported when this error occurred
  GRPC_ERROR_INT_WSA_ERROR =
      static_cast<int>(grpc_core::StatusIntProperty::kWsaError),
  /// File descriptor associated with this error
  GRPC_ERROR_INT_FD = static_cast<int>(grpc_core::StatusIntProperty::kFd),
  /// HTTP status (i.e. 404)
  GRPC_ERROR_INT_HTTP_STATUS =
      static_cast<int>(grpc_core::StatusIntProperty::kHttpStatus),
  /// chttp2: did the error occur while a write was in progress
  GRPC_ERROR_INT_OCCURRED_DURING_WRITE =
      static_cast<int>(grpc_core::StatusIntProperty::kOccurredDuringWrite),
  /// channel connectivity state associated with the error
  GRPC_ERROR_INT_CHANNEL_CONNECTIVITY_STATE =
      static_cast<int>(grpc_core::StatusIntProperty::ChannelConnectivityState),
  /// LB policy drop
  GRPC_ERROR_INT_LB_POLICY_DROP =
      static_cast<int>(grpc_core::StatusIntProperty::kLbPolicyDrop),

  /// Must always be last
  GRPC_ERROR_INT_MAX,
} grpc_error_ints;

typedef enum {
  /// top-level textual description of this error
  GRPC_ERROR_STR_DESCRIPTION =
      static_cast<int>(grpc_core::StatusStrProperty::kDescription),
  /// source file in which this error occurred
  GRPC_ERROR_STR_FILE = static_cast<int>(grpc_core::StatusStrProperty::kFile),
  /// operating system description of this error
  GRPC_ERROR_STR_OS_ERROR =
      static_cast<int>(grpc_core::StatusStrProperty::kOsError),
  /// syscall that generated this error
  GRPC_ERROR_STR_SYSCALL =
      static_cast<int>(grpc_core::StatusStrProperty::kSyscall),
  /// peer that we were trying to communicate when this error occurred
  GRPC_ERROR_STR_TARGET_ADDRESS =
      static_cast<int>(grpc_core::StatusStrProperty::kTargetAddress),
  /// grpc status message associated with this error
  GRPC_ERROR_STR_GRPC_MESSAGE =
      static_cast<int>(grpc_core::StatusStrProperty::kGrpcMessage),
  /// hex dump (or similar) with the data that generated this error
  GRPC_ERROR_STR_RAW_BYTES =
      static_cast<int>(grpc_core::StatusStrProperty::kRawBytes),
  /// tsi error string associated with this error
  GRPC_ERROR_STR_TSI_ERROR =
      static_cast<int>(grpc_core::StatusStrProperty::kTsiError),
  /// filename that we were trying to read/write when this error occurred
  GRPC_ERROR_STR_FILENAME =
      static_cast<int>(grpc_core::StatusStrProperty::kFilename),
  /// key associated with the error
  GRPC_ERROR_STR_KEY = static_cast<int>(grpc_core::StatusStrProperty::kKey),
  /// value associated with the error
  GRPC_ERROR_STR_VALUE = static_cast<int>(grpc_core::StatusStrProperty::kValue),

  /// Must always be last
  GRPC_ERROR_STR_MAX,
} grpc_error_strs;

typedef enum {
  /// timestamp of error creation
  GRPC_ERROR_TIME_CREATED,

  /// Must always be last
  GRPC_ERROR_TIME_MAX,
} grpc_error_times;

// DEPRECATED: Use grpc_error_std_string instead
const char* grpc_error_string(grpc_error_handle error);
std::string grpc_error_std_string(grpc_error_handle error);

// debug only toggles that allow for a sanity to check that ensures we will
// never create any errors in the per-RPC hotpath.
void grpc_disable_error_creation();
void grpc_enable_error_creation();

#ifdef GRPC_ERROR_IS_ABSEIL_STATUS

#define GRPC_ERROR_NONE absl::OkStatus()
#define GRPC_ERROR_OOM absl::Status(absl::ResourceExhaustedError)
#define GRPC_ERROR_CANCELLED absl::CancelledError()

#define GRPC_ERROR_REF(err) (err)
#define GRPC_ERROR_UNREF(err)

#define GRPC_ERROR_CREATE_FROM_STATIC_STRING(desc) \
  StatusCreate(absl::StatusCode::kUnknown, desc, DEBUG_LOCATION, {})
#define GRPC_ERROR_CREATE_FROM_COPIED_STRING(desc) \
  StatusCreate(absl::StatusCode::kUnknown, desc, DEBUG_LOCATION, {})
#define GRPC_ERROR_CREATE_FROM_STRING_VIEW(desc) \
  StatusCreate(ababsl::StatusCode::kUnknown, desc, DEBUG_LOCATION, {})

absl::Status grpc_status_create(absl::StatusCode code, absl::string_view msg,
                                const grpc_core::DebugLocation& location,
                                size_t children_count,
                                absl::Status* children) GRPC_MUST_USE_RESULT;

// Create an error that references some other errors. This function adds a
// reference to each error in errs - it does not consume an existing reference
#define GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(desc, errs, count)   \
  grpc_status_create(absl::StatusCode::kUnknown, desc, DEBUG_LOCATION, count, \
                     errs)
#define GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(desc, errs, count)   \
  grpc_status_create(absl::StatusCode::kUnknown, desc, DEBUG_LOCATION, count, \
                     errs)

// Consumes all the errors in the vector and forms a referencing error from
// them. If the vector is empty, return GRPC_ERROR_NONE.
template <typename VectorType>
static absl::Status grpc_status_create_from_vector(
    const grpc_core::DebugLocation& location, const char* desc,
    VectorType* error_list) {
  absl::Status error = GRPC_ERROR_NONE;
  if (error_list->size() != 0) {
    error = grpc_status_create(absl::StatusCode::kUnknown, desc, DEBUG_LOCATION,
                               error_list->size(), error_list->data());
    error_list->clear();
  }
  return error;
}

#define GRPC_ERROR_CREATE_FROM_VECTOR(desc, error_list) \
  grpc_status_create_from_vector(DEBUG_LOCATION, desc, error_list)

absl::Status grpc_os_error(const grpc_core::DebugLocation& location, int err,
                           const char* call_name) GRPC_MUST_USE_RESULT;

inline absl::Status grpc_assert_never_ok(absl::Status error) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  return error;
}

/// create an error associated with errno!=0 (an 'operating system' error)
#define GRPC_OS_ERROR(err, call_name) \
  grpc_assert_never_ok(grpc_os_error(DEBUG_LOCATION, err, call_name))

absl::Status grpc_wsa_error(const grpc_core::DebugLocation& location, int err,
                            const char* call_name) GRPC_MUST_USE_RESULT;

/// windows only: create an error associated with WSAGetLastError()!=0
#define GRPC_WSA_ERROR(err, call_name) \
  grpc_wsa_error(DEBUG_LOCATION, err, call_name)

#else  // GRPC_ERROR_IS_ABSEIL_STATUS

/// The following "special" errors can be propagated without allocating memory.
/// They are always even so that other code (particularly combiner locks,
/// polling engines) can safely use the lower bit for themselves.

#define GRPC_ERROR_NONE ((grpc_error_handle)NULL)
#define GRPC_ERROR_RESERVED_1 ((grpc_error_handle)1)
#define GRPC_ERROR_OOM ((grpc_error_handle)2)
#define GRPC_ERROR_RESERVED_2 ((grpc_error_handle)3)
#define GRPC_ERROR_CANCELLED ((grpc_error_handle)4)
#define GRPC_ERROR_SPECIAL_MAX GRPC_ERROR_CANCELLED

inline bool grpc_error_is_special(grpc_error_handle err) {
  return err <= GRPC_ERROR_SPECIAL_MAX;
}

#ifndef NDEBUG
grpc_error_handle grpc_error_do_ref(grpc_error_handle err, const char* file,
                                    int line);
void grpc_error_do_unref(grpc_error_handle err, const char* file, int line);
inline grpc_error_handle grpc_error_ref(grpc_error_handle err, const char* file,
                                        int line) {
  if (grpc_error_is_special(err)) return err;
  return grpc_error_do_ref(err, file, line);
}
inline void grpc_error_unref(grpc_error_handle err, const char* file,
                             int line) {
  if (grpc_error_is_special(err)) return;
  grpc_error_do_unref(err, file, line);
}
#define GRPC_ERROR_REF(err) grpc_error_ref(err, __FILE__, __LINE__)
#define GRPC_ERROR_UNREF(err) grpc_error_unref(err, __FILE__, __LINE__)
#else
grpc_error_handle grpc_error_do_ref(grpc_error_handle err);
void grpc_error_do_unref(grpc_error_handle err);
inline grpc_error_handle grpc_error_ref(grpc_error_handle err) {
  if (grpc_error_is_special(err)) return err;
  return grpc_error_do_ref(err);
}
inline void grpc_error_unref(grpc_error_handle err) {
  if (grpc_error_is_special(err)) return;
  grpc_error_do_unref(err);
}
#define GRPC_ERROR_REF(err) grpc_error_ref(err)
#define GRPC_ERROR_UNREF(err) grpc_error_unref(err)
#endif

/// Create an error - but use GRPC_ERROR_CREATE instead
grpc_error_handle grpc_error_create(const char* file, int line,
                                    const grpc_slice& desc,
                                    grpc_error_handle* referencing,
                                    size_t num_referencing);
/// Create an error (this is the preferred way of generating an error that is
///   not due to a system call - for system calls, use GRPC_OS_ERROR or
///   GRPC_WSA_ERROR as appropriate)
/// \a referencing is an array of num_referencing elements indicating one or
/// more errors that are believed to have contributed to this one
/// err = grpc_error_create(x, y, z, r, nr) is equivalent to:
///   err = grpc_error_create(x, y, z, NULL, 0);
///   for (i=0; i<nr; i++) err = grpc_error_add_child(err, r[i]);
#define GRPC_ERROR_CREATE_FROM_STATIC_STRING(desc)                           \
  grpc_error_create(__FILE__, __LINE__, grpc_slice_from_static_string(desc), \
                    NULL, 0)
#define GRPC_ERROR_CREATE_FROM_COPIED_STRING(desc)                           \
  grpc_error_create(__FILE__, __LINE__, grpc_slice_from_copied_string(desc), \
                    NULL, 0)
#define GRPC_ERROR_CREATE_FROM_STRING_VIEW(desc) \
  grpc_error_create(                             \
      __FILE__, __LINE__,                        \
      grpc_slice_from_copied_buffer((desc).data(), (desc).size()), NULL, 0)

// Create an error that references some other errors. This function adds a
// reference to each error in errs - it does not consume an existing reference
#define GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(desc, errs, count)  \
  grpc_error_create(__FILE__, __LINE__, grpc_slice_from_static_string(desc), \
                    errs, count)
#define GRPC_ERROR_CREATE_REFERENCING_FROM_COPIED_STRING(desc, errs, count)  \
  grpc_error_create(__FILE__, __LINE__, grpc_slice_from_copied_string(desc), \
                    errs, count)

#define GRPC_ERROR_CREATE_FROM_VECTOR(desc, error_list) \
  grpc_error_create_from_vector(__FILE__, __LINE__, desc, error_list)

// Consumes all the errors in the vector and forms a referencing error from
// them. If the vector is empty, return GRPC_ERROR_NONE.
template <typename VectorType>
static grpc_error_handle grpc_error_create_from_vector(const char* file,
                                                       int line,
                                                       const char* desc,
                                                       VectorType* error_list) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (error_list->size() != 0) {
    error = grpc_error_create(file, line, grpc_slice_from_static_string(desc),
                              error_list->data(), error_list->size());
    // Remove refs to all errors in error_list.
    for (size_t i = 0; i < error_list->size(); i++) {
      GRPC_ERROR_UNREF((*error_list)[i]);
    }
    error_list->clear();
  }
  return error;
}

grpc_error_handle grpc_os_error(const char* file, int line, int err,
                                const char* call_name) GRPC_MUST_USE_RESULT;

inline grpc_error_handle grpc_assert_never_ok(grpc_error_handle error) {
  GPR_ASSERT(error != GRPC_ERROR_NONE);
  return error;
}

/// create an error associated with errno!=0 (an 'operating system' error)
#define GRPC_OS_ERROR(err, call_name) \
  grpc_assert_never_ok(grpc_os_error(__FILE__, __LINE__, err, call_name))
grpc_error_handle grpc_wsa_error(const char* file, int line, int err,
                                 const char* call_name) GRPC_MUST_USE_RESULT;
/// windows only: create an error associated with WSAGetLastError()!=0
#define GRPC_WSA_ERROR(err, call_name) \
  grpc_wsa_error(__FILE__, __LINE__, err, call_name)

#endif  // GRPC_ERROR_IS_ABSEIL_STATUS

grpc_error_handle grpc_error_set_int(grpc_error_handle src,
                                     grpc_error_ints which,
                                     intptr_t value) GRPC_MUST_USE_RESULT;
/// It is an error to pass nullptr as `p`. Caller should allocate a phony
/// intptr_t for `p`, even if the value of `p` is not used.
bool grpc_error_get_int(grpc_error_handle error, grpc_error_ints which,
                        intptr_t* p);
/// This call takes ownership of the slice; the error is responsible for
/// eventually unref-ing it.
grpc_error_handle grpc_error_set_str(
    grpc_error_handle src, grpc_error_strs which,
    const grpc_slice& str) GRPC_MUST_USE_RESULT;
/// Returns false if the specified string is not set.
/// Caller does NOT own the slice.
bool grpc_error_get_str(grpc_error_handle error, grpc_error_strs which,
                        grpc_slice* s);

/// Add a child error: an error that is believed to have contributed to this
/// error occurring. Allows root causing high level errors from lower level
/// errors that contributed to them. The src error takes ownership of the
/// child error.
///
/// Edge Conditions -
/// 1) If either of \a src or \a child is GRPC_ERROR_NONE, returns a reference
/// to the other argument. 2) If both \a src and \a child are GRPC_ERROR_NONE,
/// returns GRPC_ERROR_NONE. 3) If \a src and \a child point to the same error,
/// returns a single reference. (Note that, 2 references should have been
/// received to the error in this case.)
grpc_error_handle grpc_error_add_child(
    grpc_error_handle src, grpc_error_handle child) GRPC_MUST_USE_RESULT;

bool grpc_log_error(const char* what, grpc_error_handle error, const char* file,
                    int line);
inline bool grpc_log_if_error(const char* what, grpc_error_handle error,
                              const char* file, int line) {
  return error == GRPC_ERROR_NONE ? true
                                  : grpc_log_error(what, error, file, line);
}

#define GRPC_LOG_IF_ERROR(what, error) \
  (grpc_log_if_error((what), (error), __FILE__, __LINE__))

#endif /* GRPC_CORE_LIB_IOMGR_ERROR_H */
