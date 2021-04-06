//
//
// Copyright 2021 the gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/time_util.h"
#include "src/core/lib/iomgr/status_util.h"

#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"

#ifdef GPR_WINDOWS
#include <grpc/support/log_windows.h>
#endif

extern const char* error_int_name(grpc_error_ints key);
extern const char* error_str_name(grpc_error_strs key);

namespace grpc_core {

absl::Status grpc_status_create(absl::StatusCode code, absl::string_view msg,
                                const char* file, int line,
                                std::initializer_list<absl::Status> children) {
  absl::Status s(code, msg);
  grpc_status_set_str(&s, GRPC_ERROR_STR_FILE, file);
  grpc_status_set_int(&s, GRPC_ERROR_INT_FILE_LINE, line);
  absl::Time now = grpc_core::ToAbslTime(gpr_now(GPR_CLOCK_REALTIME));
  s.SetPayload("created", absl::Cord(absl::FormatTime(now)));
  for (const absl::Status& child : children) {
    if (!child.ok()) {
      grpc_status_add_child(&s, child);
    }
  }
  return s;
}

void grpc_status_set_int(absl::Status* status, grpc_error_ints which,
                         intptr_t value) {
  status->SetPayload(error_int_name(which), absl::Cord(std::to_string(value)));
}

absl::optional<intptr_t> grpc_status_get_int(const absl::Status& status,
                                             grpc_error_ints which) {
  absl::optional<absl::Cord> p = status.GetPayload(error_int_name(which));
  if (p.has_value()) {
    absl::optional<absl::string_view> sv = p->TryFlat();
    intptr_t value;
    if (sv.has_value()) {
      if (absl::SimpleAtoi(*sv, &value)) {
        return value;
      }
    } else {
      if (absl::SimpleAtoi(std::string(*p), &value)) {
        return value;
      }
    }
  }
  return {};
}

void grpc_status_set_str(absl::Status* status, grpc_error_strs which,
                         std::string value) {
  status->SetPayload(error_str_name(which), absl::Cord(value));
}

absl::optional<std::string> grpc_status_get_str(const absl::Status& status,
                                                grpc_error_strs which) {
  absl::optional<absl::Cord> p = status.GetPayload(error_str_name(which));
  if (p.has_value()) {
    return std::string(*p);
  }
  return {};
}

void grpc_status_add_child(absl::Status* status, absl::Status child) {
  std::string child_str = grpc_status_to_string(child);
  absl::optional<absl::Cord> children = status->GetPayload("children");
  if (children.has_value()) {
    std::string children_str = std::string(*children);
    absl::StrAppend(&children_str, ", ", child_str);
    status->SetPayload("children", absl::Cord(children_str));
  } else {
    status->SetPayload("children", absl::Cord(child_str));
  }
}

std::string grpc_status_to_string(const absl::Status& status) {
  if (status.ok()) {
    return "OK";
  }

  std::string text;
  absl::StrAppend(&text, absl::StatusCodeToString(status.code()));
  if (!status.message().empty()) {
    absl::StrAppend(&text, ":", status.message());
  }

  bool isFirst = true;
  absl::optional<absl::Cord> children;
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (type_url == "children") {
          children = payload;
          return;
        }
        if (isFirst) {
          absl::StrAppend(&text, " {");
          isFirst = false;
        } else {
          absl::StrAppend(&text, ", ");
        }
        absl::StrAppend(&text, type_url, ":'",
                        absl::CHexEscape(std::string(payload)), "'");
      });
  if (children) {
    if (isFirst) {
      absl::StrAppend(&text, " {");
      isFirst = false;
    } else {
      absl::StrAppend(&text, ", ");
    }
    absl::StrAppend(&text, "children:[", std::string(*children), "]");
  }
  if (!isFirst) {
    absl::StrAppend(&text, "}");
  }
  return text;
}

void grpc_log_status(const char* what, absl::Status status, const char* file,
                     int line) {
  GPR_DEBUG_ASSERT(!status.ok());
  std::string status_text = grpc_status_to_string(status);
  gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "%s: %s", what,
          status_text.c_str());
}

absl::Status grpc_status_os_create(const char* file, int line, int err,
                                   const char* call_name) {
  absl::Status s = grpc_status_create(absl::StatusCode::kInternal,
                                      strerror(err), file, line, {});
  grpc_status_set_int(&s, GRPC_ERROR_INT_ERRNO, err);
  grpc_status_set_str(&s, GRPC_ERROR_STR_OS_ERROR, strerror(err));
  grpc_status_set_str(&s, GRPC_ERROR_STR_SYSCALL, call_name);
  return s;
}

#ifdef GPR_WINDOWS
absl::Status grpc_status_wsa_create(const char* file, int line, int err,
                                    const char* call_name) {
  absl::Status s = grpc_status_create(absl::StatusCode::kInternal, "WSA Error",
                                      file, line, {});
  char* utf8_message = gpr_format_message(err);
  grpc_status_set_int(&s, GRPC_ERROR_INT_WSA_ERROR, err);
  grpc_status_set_str(&s, GRPC_ERROR_STR_OS_ERROR, utf8_message);
  grpc_status_set_str(&s, GRPC_ERROR_STR_SYSCALL, call_name);
  return s;
}
#endif  // GPR_WINDOWS

}  // namespace grpc_core
