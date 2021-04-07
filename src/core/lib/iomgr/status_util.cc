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
#include "absl/strings/str_join.h"

#ifdef GPR_WINDOWS
#include <grpc/support/log_windows.h>
#endif

extern const char* error_int_name(grpc_error_ints key);
extern const char* error_str_name(grpc_error_strs key);

namespace grpc_core {

absl::Status StatusCreate(absl::StatusCode code, absl::string_view msg,
                          const char* file, int line,
                          std::initializer_list<absl::Status> children) {
  absl::Status s(code, msg);
  StatusSetStr(&s, GRPC_ERROR_STR_FILE, file);
  StatusSetInt(&s, GRPC_ERROR_INT_FILE_LINE, line);
  absl::Time now = grpc_core::ToAbslTime(gpr_now(GPR_CLOCK_REALTIME));
  s.SetPayload("created", absl::Cord(absl::FormatTime(now)));
  for (const absl::Status& child : children) {
    if (!child.ok()) {
      StatusAddChild(&s, child);
    }
  }
  return s;
}

void StatusSetInt(absl::Status* status, grpc_error_ints which, intptr_t value) {
  status->SetPayload(error_int_name(which), absl::Cord(std::to_string(value)));
}

absl::optional<intptr_t> StatusGetInt(const absl::Status& status,
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

void StatusSetStr(absl::Status* status, grpc_error_strs which,
                  std::string value) {
  status->SetPayload(error_str_name(which), absl::Cord(value));
}

absl::optional<std::string> StatusGetStr(const absl::Status& status,
                                         grpc_error_strs which) {
  absl::optional<absl::Cord> p = status.GetPayload(error_str_name(which));
  if (p.has_value()) {
    return std::string(*p);
  }
  return {};
}

void StatusAddChild(absl::Status* status, absl::Status child) {
  std::string child_str = StatusToString(child);
  absl::optional<absl::Cord> children = status->GetPayload("children");
  if (children.has_value()) {
    children->Append(", ");
    children->Append(std::move(child_str));
    status->SetPayload("children", *children);
  } else {
    status->SetPayload("children", absl::Cord(std::move(child_str)));
  }
}

std::string StatusToString(const absl::Status& status) {
  if (status.ok()) {
    return "OK";
  }
  std::string head;
  absl::StrAppend(&head, absl::StatusCodeToString(status.code()));
  if (!status.message().empty()) {
    absl::StrAppend(&head, ":", status.message());
  }
  std::vector<std::string> kvs;
  absl::optional<absl::Cord> children;
  status.ForEachPayload([&](absl::string_view type_url,
                            const absl::Cord& payload) {
    if (type_url == "children") {
      children = payload;
    } else {
      absl::optional<absl::string_view> payload_view = payload.TryFlat();
      std::string payload_str = absl::CHexEscape(
          payload_view.has_value() ? *payload_view : std::string(payload));
      kvs.push_back(absl::StrCat(type_url, ":'", std::move(payload_str), "'"));
    }
  });
  if (children.has_value()) {
    absl::optional<absl::string_view> children_view = children->TryFlat();
    kvs.push_back(absl::StrCat(
        "children:[",
        children_view.has_value() ? *children_view : std::string(*children),
        "]"));
  }
  return kvs.empty() ? head
                     : absl::StrCat(head, " {", absl::StrJoin(kvs, ", "), "}");
}

absl::Status StatusCreateOS(const char* file, int line, int err,
                            const char* call_name) {
  absl::Status s =
      StatusCreate(absl::StatusCode::kInternal, strerror(err), file, line, {});
  StatusSetInt(&s, GRPC_ERROR_INT_ERRNO, err);
  StatusSetStr(&s, GRPC_ERROR_STR_OS_ERROR, strerror(err));
  StatusSetStr(&s, GRPC_ERROR_STR_SYSCALL, call_name);
  return s;
}

#ifdef GPR_WINDOWS
absl::Status StatusCreateWSA(const char* file, int line, int err,
                             const char* call_name) {
  absl::Status s =
      StatusCreate(absl::StatusCode::kInternal, "WSA Error", file, line, {});
  char* utf8_message = gpr_format_message(err);
  StatusSetInt(&s, GRPC_ERROR_INT_WSA_ERROR, err);
  StatusSetStr(&s, GRPC_ERROR_STR_OS_ERROR, utf8_message);
  StatusSetStr(&s, GRPC_ERROR_STR_SYSCALL, call_name);
  return s;
}
#endif  // GPR_WINDOWS

}  // namespace grpc_core
