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

#include <inttypes.h>
#include <string.h>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/crash.h"

#ifdef GPR_WINDOWS
#include <grpc/support/log_windows.h>
#endif

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/strerror.h"
#include "src/core/lib/slice/slice_internal.h"

grpc_core::DebugOnlyTraceFlag grpc_trace_error_refcount(false,
                                                        "error_refcount");
grpc_core::DebugOnlyTraceFlag grpc_trace_closure(false, "closure");

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
  auto err_string = grpc_core::StrError(err);
  absl::Status s =
      StatusCreate(absl::StatusCode::kUnknown, err_string, location, {});
  grpc_core::StatusSetInt(&s, grpc_core::StatusIntProperty::kErrorNo, err);
  grpc_core::StatusSetStr(&s, grpc_core::StatusStrProperty::kOsError,
                          err_string);
  grpc_core::StatusSetStr(&s, grpc_core::StatusStrProperty::kSyscall,
                          call_name);
  return s;
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
  absl::Status s = StatusCreate(absl::StatusCode::kUnavailable,
                                WSAErrorToShortDescription(err), location, {});
  StatusSetInt(&s, grpc_core::StatusIntProperty::kWsaError, err);
  StatusSetInt(&s, grpc_core::StatusIntProperty::kRpcStatus,
               GRPC_STATUS_UNAVAILABLE);
  StatusSetStr(&s, grpc_core::StatusStrProperty::kOsError, utf8_message);
  StatusSetStr(&s, grpc_core::StatusStrProperty::kSyscall, call_name);
  gpr_free(utf8_message);
  return s;
}
#endif

grpc_error_handle grpc_error_set_int(grpc_error_handle src,
                                     grpc_core::StatusIntProperty which,
                                     intptr_t value) {
  if (src.ok()) {
    src = absl::UnknownError("");
    StatusSetInt(&src, grpc_core::StatusIntProperty::kRpcStatus,
                 GRPC_STATUS_OK);
  }
  grpc_core::StatusSetInt(&src, which, value);
  return src;
}

bool grpc_error_get_int(grpc_error_handle error,
                        grpc_core::StatusIntProperty which, intptr_t* p) {
  absl::optional<intptr_t> value = grpc_core::StatusGetInt(error, which);
  if (value.has_value()) {
    *p = *value;
    return true;
  } else {
    // TODO(veblush): Remove this once absl::Status migration is done
    if (which == grpc_core::StatusIntProperty::kRpcStatus) {
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
  if (src.ok()) {
    src = absl::UnknownError("");
    StatusSetInt(&src, grpc_core::StatusIntProperty::kRpcStatus,
                 GRPC_STATUS_OK);
  }
  if (which == grpc_core::StatusStrProperty::kDescription) {
    // To change the message of absl::Status, a new instance should be created
    // with a code and payload because it doesn't have a setter for it.
    absl::Status s = absl::Status(src.code(), str);
    src.ForEachPayload(
        [&](absl::string_view type_url, const absl::Cord& payload) {
          s.SetPayload(type_url, payload);
        });
    return s;
  } else {
    grpc_core::StatusSetStr(&src, which, str);
  }
  return src;
}

bool grpc_error_get_str(grpc_error_handle error,
                        grpc_core::StatusStrProperty which, std::string* s) {
  if (which == grpc_core::StatusStrProperty::kDescription) {
    // absl::Status uses the message field for
    // grpc_core::StatusStrProperty::kDescription instead of using payload.
    absl::string_view msg = error.message();
    if (msg.empty()) {
      return false;
    } else {
      *s = std::string(msg);
      return true;
    }
  } else {
    absl::optional<std::string> value = grpc_core::StatusGetStr(error, which);
    if (value.has_value()) {
      *s = std::move(*value);
      return true;
    } else {
      // TODO(veblush): Remove this once absl::Status migration is done
      if (which == grpc_core::StatusStrProperty::kGrpcMessage) {
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
}

grpc_error_handle grpc_error_add_child(grpc_error_handle src,
                                       grpc_error_handle child) {
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
  gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "%s: %s", what,
          grpc_core::StatusToString(error).c_str());
  return false;
}
