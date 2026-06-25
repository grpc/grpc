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

#include "src/core/util/status_helper.h"

#include <grpc/status.h>
#include <string.h>

#include <utility>

#include "google/protobuf/any.upb.h"
#include "google/rpc/status.upb.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/slice/percent_encoding.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/status_conversion.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"
#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"

namespace grpc_core {

namespace {

#define TYPE_URL_PREFIX "type.googleapis.com/grpc.status."
#define TYPE_INT_TAG "int."
#define TYPE_URL(name) (TYPE_URL_PREFIX name)
const absl::string_view kTypeUrlPrefix = TYPE_URL_PREFIX;
const absl::string_view kTypeIntTag = TYPE_INT_TAG;

const char* GetStatusIntPropertyUrl(StatusIntProperty key) {
  switch (key) {
    case StatusIntProperty::kStreamId:
      return TYPE_URL(TYPE_INT_TAG "stream_id");
    case StatusIntProperty::kRpcStatus:
      return TYPE_URL(TYPE_INT_TAG "grpc_status");
    case StatusIntProperty::kHttp2Error:
      return TYPE_URL(TYPE_INT_TAG "http2_error");
    case StatusIntProperty::ChannelConnectivityState:
      return TYPE_URL(TYPE_INT_TAG "channel_connectivity_state");
    case StatusIntProperty::kLbPolicyDrop:
      return TYPE_URL(TYPE_INT_TAG "lb_policy_drop");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

}  // namespace

absl::Status StatusCreate(absl::StatusCode code, absl::string_view msg,
                          const DebugLocation& /*location*/,
                          std::vector<absl::Status> children) {
  absl::Status s(code, msg);
  for (const absl::Status& child : children) {
    if (!child.ok()) {
      StatusAddChild(&s, child);
    }
  }
  return s;
}

namespace {

absl::Status ReplaceStatusCode(const absl::Status& status,
                               absl::StatusCode code) {
  absl::Status new_status(code, status.message());
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        new_status.SetPayload(type_url, payload);
      });
  return new_status;
}

}  // namespace

void StatusSetInt(absl::Status* status, StatusIntProperty key, intptr_t value) {
  if (key == StatusIntProperty::kRpcStatus) {
    // When setting the RPC status, just replace the top-level status code.
    *status = ReplaceStatusCode(*status, static_cast<absl::StatusCode>(value));
    return;
  }
  status->SetPayload(GetStatusIntPropertyUrl(key),
                     absl::Cord(std::to_string(value)));
}

std::optional<intptr_t> StatusGetInt(const absl::Status& status,
                                     StatusIntProperty key) {
  if (key == StatusIntProperty::kRpcStatus) {
    return static_cast<intptr_t>(status.code());
  }
  auto p = status.GetPayload(GetStatusIntPropertyUrl(key));
  if (p.has_value()) {
    auto sv = p->TryFlat();
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

void StatusAddChild(absl::Status* status, absl::Status child) {
  // If the child is OK, there's nothing to do.
  if (child.ok()) return;
  // If the parent is OK, replace it with the child.
  if (status->ok()) {
    *status = std::move(child);
    return;
  }
  // Parent and child are both non-OK, so we need to merge.
  absl::Status new_status(
      // Prefer any other code over UNKNOWN.
      status->code() == absl::StatusCode::kUnknown ? child.code()
                                                   : status->code(),
      absl::StrCat(status->message(), " (", child.message(), ")"));
  // TODO(roth): Remove this once we eliminate all status attributes.
  status->ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        new_status.SetPayload(type_url, payload);
      });
  child.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        // If the original error already has the attribute, don't overwrite
        // it.
        if (!new_status.GetPayload(type_url).has_value()) {
          new_status.SetPayload(type_url, payload);
        }
      });
  *status = std::move(new_status);
}

std::vector<absl::Status> StatusGetChildren(absl::Status status) { return {}; }

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
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (absl::StartsWith(type_url, kTypeUrlPrefix)) {
          type_url.remove_prefix(kTypeUrlPrefix.size());
          absl::string_view payload_view;
          std::string payload_storage;
          if (payload.TryFlat().has_value()) {
            payload_view = payload.TryFlat().value();
          } else {
            payload_storage = std::string(payload);
            payload_view = payload_storage;
          }
          if (absl::StartsWith(type_url, kTypeIntTag)) {
            type_url.remove_prefix(kTypeIntTag.size());
            kvs.push_back(absl::StrCat(type_url, ":", payload_view));
          } else {
            kvs.push_back(absl::StrCat(type_url, ":\"",
                                       absl::CHexEscape(payload_view), "\""));
          }
        } else {
          auto payload_view = payload.TryFlat();
          std::string payload_str = absl::CHexEscape(
              payload_view.has_value() ? *payload_view : std::string(payload));
          kvs.push_back(absl::StrCat(type_url, ":\"", payload_str, "\""));
        }
      });
  return kvs.empty() ? head
                     : absl::StrCat(head, " {", absl::StrJoin(kvs, ", "), "}");
}

namespace {

absl::Status ReplaceStatusMessage(const absl::Status& status,
                                  absl::string_view message) {
  absl::Status new_status(status.code(), message);
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        new_status.SetPayload(type_url, payload);
      });
  return new_status;
}

}  // namespace

absl::Status AddMessagePrefix(absl::string_view prefix,
                              const absl::Status& status) {
  return ReplaceStatusMessage(status,
                              absl::StrCat(prefix, ": ", status.message()));
}

absl::Status AddMessageDetail(absl::string_view detail,
                              const absl::Status& status) {
  return ReplaceStatusMessage(
      status, absl::StrCat(status.message(), " (", detail, ")"));
}

namespace internal {

google_rpc_Status* StatusToProto(const absl::Status& status, upb_Arena* arena) {
  google_rpc_Status* msg = google_rpc_Status_new(arena);
  google_rpc_Status_set_code(msg, static_cast<int32_t>(status.code()));
  // Protobuf string field requires to be utf-8 encoding but C++ string doesn't
  // this requirement so it can be a non utf-8 string. So it should be converted
  // to a percent-encoded string to keep it as a utf-8 string.
  Slice message_percent_slice =
      PercentEncodeSlice(Slice::FromExternalString(status.message()),
                         PercentEncodingType::Compatible);
  char* message_percent = reinterpret_cast<char*>(
      upb_Arena_Malloc(arena, message_percent_slice.length()));
  if (!message_percent_slice.empty()) {
    memcpy(message_percent, message_percent_slice.data(),
           message_percent_slice.length());
  }
  google_rpc_Status_set_message(
      msg, upb_StringView_FromDataAndSize(message_percent,
                                          message_percent_slice.length()));
  status.ForEachPayload([&](absl::string_view type_url,
                            const absl::Cord& payload) {
    google_protobuf_Any* any = google_rpc_Status_add_details(msg, arena);
    char* type_url_buf =
        reinterpret_cast<char*>(upb_Arena_Malloc(arena, type_url.size()));
    memcpy(type_url_buf, type_url.data(), type_url.size());
    google_protobuf_Any_set_type_url(
        any, upb_StringView_FromDataAndSize(type_url_buf, type_url.size()));
    auto v_view = payload.TryFlat();
    if (v_view.has_value()) {
      google_protobuf_Any_set_value(
          any, upb_StringView_FromDataAndSize(v_view->data(), v_view->size()));
    } else {
      char* buf =
          reinterpret_cast<char*>(upb_Arena_Malloc(arena, payload.size()));
      char* cur = buf;
      for (absl::string_view chunk : payload.Chunks()) {
        memcpy(cur, chunk.data(), chunk.size());
        cur += chunk.size();
      }
      google_protobuf_Any_set_value(
          any, upb_StringView_FromDataAndSize(buf, payload.size()));
    }
  });
  return msg;
}

absl::Status StatusFromProto(google_rpc_Status* msg) {
  int32_t code = google_rpc_Status_code(msg);
  upb_StringView message_percent_upb = google_rpc_Status_message(msg);
  Slice message_percent_slice = Slice::FromExternalString(
      absl::string_view(message_percent_upb.data, message_percent_upb.size));
  Slice message_slice =
      PermissivePercentDecodeSlice(std::move(message_percent_slice));
  absl::Status status(
      static_cast<absl::StatusCode>(code),
      absl::string_view(reinterpret_cast<const char*>(message_slice.data()),
                        message_slice.size()));
  size_t detail_len;
  const google_protobuf_Any* const* details =
      google_rpc_Status_details(msg, &detail_len);
  for (size_t i = 0; i < detail_len; i++) {
    upb_StringView type_url = google_protobuf_Any_type_url(details[i]);
    upb_StringView value = google_protobuf_Any_value(details[i]);
    status.SetPayload(absl::string_view(type_url.data, type_url.size),
                      absl::Cord(absl::string_view(value.data, value.size)));
  }
  return status;
}

uintptr_t StatusAllocHeapPtr(absl::Status s) {
  if (s.ok()) return 0;
  absl::Status* ptr = new absl::Status(s);
  return reinterpret_cast<uintptr_t>(ptr);
}

void StatusFreeHeapPtr(uintptr_t ptr) {
  absl::Status* s = reinterpret_cast<absl::Status*>(ptr);
  delete s;
}

absl::Status StatusGetFromHeapPtr(uintptr_t ptr) {
  if (ptr == 0) {
    return absl::OkStatus();
  } else {
    return *reinterpret_cast<absl::Status*>(ptr);
  }
}

absl::Status StatusMoveFromHeapPtr(uintptr_t ptr) {
  if (ptr == 0) {
    return absl::OkStatus();
  } else {
    absl::Status* s = reinterpret_cast<absl::Status*>(ptr);
    absl::Status ret = std::move(*s);
    delete s;
    return ret;
  }
}

}  // namespace internal

}  // namespace grpc_core
