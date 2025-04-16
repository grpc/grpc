//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//
#include "src/core/lib/iomgr/error.h"

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <inttypes.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/core/util/crash.h"

#ifdef GPR_WINDOWS
#include <grpc/support/log_windows.h>
#endif

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/util/strerror.h"
#include "src/core/util/useful.h"

absl::Status grpc_status_create(absl::StatusCode code, absl::string_view msg,
                                const grpc_core::DebugLocation& location,
                                size_t children_count, absl::Status* children) {
  absl::Status s = StatusCreate(code, msg, location, {});
  for (size_t i = 0; i < children_count; ++i) {
    if (!children[i].ok()) {
      grpc_core::StatusAddChild(&s, children[i]);
    }
  }
  return s;
}

absl::Status grpc_os_error(const grpc_core::DebugLocation& location, int err,
                           const char* call_name) {
  return StatusCreate(
      absl::StatusCode::kUnknown,
      absl::StrCat(call_name, ": ", grpc_core::StrError(err), " (", err, ")"),
      location, {});
}

#ifdef GPR_WINDOWS
std::string WSAErrorToShortDescription(int err) {
  switch (err) {
    case WSAEACCES:
      return "Permission denied";
    case WSAEFAULT:
      return "Bad address";
    case WSAEMFILE:
      return "Too many open files";
    case WSAEMSGSIZE:
      return "Message too long";
    case WSAENETDOWN:
      return "Network is down";
    case WSAENETUNREACH:
      return "Network is unreachable";
    case WSAENETRESET:
      return "Network dropped connection on reset";
    case WSAECONNABORTED:
      return "Connection aborted";
    case WSAECONNRESET:
      return "Connection reset";
    case WSAETIMEDOUT:
      return "Connection timed out";
    case WSAECONNREFUSED:
      return "Connection refused";
    case WSAEHOSTUNREACH:
      return "No route to host";
    default:
      return "WSA Error";
  };
}
// TODO(veblush): lift out of iomgr for use in the WindowsEventEngine
absl::Status grpc_wsa_error(const grpc_core::DebugLocation& location, int err,
                            absl::string_view call_name) {
  char* utf8_message = gpr_format_message(err);
  absl::Status status = StatusCreate(
      absl::StatusCode::kUnavailable,
      absl::StrCat(call_name, ": ", WSAErrorToShortDescription(err), " (",
                   utf8_message, " -- ", err, ")"),
      location, {});
  StatusSetInt(&status, grpc_core::StatusIntProperty::kRpcStatus,
               GRPC_STATUS_UNAVAILABLE);
  gpr_free(utf8_message);
  return status;
}
#endif

grpc_error_handle grpc_error_set_int(grpc_error_handle src,
                                     grpc_core::StatusIntProperty which,
                                     intptr_t value) {
  if (!grpc_core::IsErrorFlattenEnabled() && src.ok()) {
    src = absl::UnknownError("");
    StatusSetInt(&src, grpc_core::StatusIntProperty::kRpcStatus,
                 GRPC_STATUS_OK);
  }
  grpc_core::StatusSetInt(&src, which, value);
  return src;
}

bool grpc_error_get_int(grpc_error_handle error,
                        grpc_core::StatusIntProperty which, intptr_t* p) {
  std::optional<intptr_t> value = grpc_core::StatusGetInt(error, which);
  if (value.has_value()) {
    *p = *value;
    return true;
  } else {
    // TODO(veblush): Remove this once absl::Status migration is done
    if (!grpc_core::IsErrorFlattenEnabled() &&
        which == grpc_core::StatusIntProperty::kRpcStatus) {
      switch (error.code()) {
        case absl::StatusCode::kOk:
          *p = GRPC_STATUS_OK;
          return true;
        case absl::StatusCode::kResourceExhausted:
          *p = GRPC_STATUS_RESOURCE_EXHAUSTED;
          return true;
        case absl::StatusCode::kCancelled:
          *p = GRPC_STATUS_CANCELLED;
          return true;
        default:
          break;
      }
    }
    return false;
  }
}

grpc_error_handle grpc_error_set_str(grpc_error_handle src,
                                     grpc_core::StatusStrProperty which,
                                     absl::string_view str) {
  if (!grpc_core::IsErrorFlattenEnabled() && src.ok()) {
    src = absl::UnknownError("");
    StatusSetInt(&src, grpc_core::StatusIntProperty::kRpcStatus,
                 GRPC_STATUS_OK);
  }
  grpc_core::StatusSetStr(&src, which, str);
  return src;
}

bool grpc_error_get_str(grpc_error_handle error,
                        grpc_core::StatusStrProperty which, std::string* s) {
  std::optional<std::string> value = grpc_core::StatusGetStr(error, which);
  if (value.has_value()) {
    *s = std::move(*value);
    return true;
  } else {
    // TODO(veblush): Remove this once absl::Status migration is done
    if (!grpc_core::IsErrorFlattenEnabled() &&
        which == grpc_core::StatusStrProperty::kGrpcMessage) {
      switch (error.code()) {
        case absl::StatusCode::kOk:
          *s = "";
          return true;
        case absl::StatusCode::kCancelled:
          *s = "CANCELLED";
          return true;
        default:
          break;
      }
    }
    return false;
  }
}

grpc_error_handle grpc_error_add_child(grpc_error_handle src,
                                       grpc_error_handle child) {
  if (grpc_core::IsErrorFlattenEnabled()) {
    grpc_core::StatusAddChild(&src, child);
    return src;
  }
  if (src.ok()) {
    return child;
  } else {
    if (!child.ok()) {
      grpc_core::StatusAddChild(&src, child);
    }
    return src;
  }
}

bool grpc_log_error(const char* what, grpc_error_handle error, const char* file,
                    int line) {
  DCHECK(!error.ok());
  LOG(ERROR).AtLocation(file, line)
      << what << ": " << grpc_core::StatusToString(error);
  return false;
}
