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

#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time_util.h"

#include <grpc/support/log.h>

#include "absl/strings/cord.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"

#include "google/protobuf/any.upb.h"
#include "google/rpc/status.upb.h"
#include "upb/upb.hpp"

namespace grpc_core {

namespace {

#define TYPE_URL_PREFIX "type.googleapis.com/grpc.status."
#define TYPE_INT_TAG "int."
#define TYPE_STR_TAG "str."
#define TYPE_TIME_TAG "time."
#define TYPE_CHILDREN_TAG "children"
#define TYPE_URL(name) (TYPE_URL_PREFIX name)
const absl::string_view kTypeUrlPrefix = TYPE_URL_PREFIX;
const absl::string_view kTypeIntTag = TYPE_INT_TAG;
const absl::string_view kTypeStrTag = TYPE_STR_TAG;
const absl::string_view kTypeTimeTag = TYPE_TIME_TAG;
const absl::string_view kTypeChildrenTag = TYPE_CHILDREN_TAG;
const absl::string_view kChildrenPropertyUrl = TYPE_URL(TYPE_CHILDREN_TAG);

const char* GetStatusIntPropertyUrl(StatusIntProperty key) {
  switch (key) {
    case StatusIntProperty::kErrorNo:
      return TYPE_URL(TYPE_INT_TAG "errno");
    case StatusIntProperty::kFileLine:
      return TYPE_URL(TYPE_INT_TAG "file_line");
    case StatusIntProperty::kStreamId:
      return TYPE_URL(TYPE_INT_TAG "stream_id");
    case StatusIntProperty::kRpcStatus:
      return TYPE_URL(TYPE_INT_TAG "grpc_status");
    case StatusIntProperty::kOffset:
      return TYPE_URL(TYPE_INT_TAG "offset");
    case StatusIntProperty::kIndex:
      return TYPE_URL(TYPE_INT_TAG "index");
    case StatusIntProperty::kSize:
      return TYPE_URL(TYPE_INT_TAG "size");
    case StatusIntProperty::kHttp2Error:
      return TYPE_URL(TYPE_INT_TAG "http2_error");
    case StatusIntProperty::kTsiCode:
      return TYPE_URL(TYPE_INT_TAG "tsi_code");
    case StatusIntProperty::kWsaError:
      return TYPE_URL(TYPE_INT_TAG "wsa_error");
    case StatusIntProperty::kFd:
      return TYPE_URL(TYPE_INT_TAG "fd");
    case StatusIntProperty::kHttpStatus:
      return TYPE_URL(TYPE_INT_TAG "http_status");
    case StatusIntProperty::kOccurredDuringWrite:
      return TYPE_URL(TYPE_INT_TAG "occurred_during_write");
    case StatusIntProperty::ChannelConnectivityState:
      return TYPE_URL(TYPE_INT_TAG "channel_connectivity_state");
    case StatusIntProperty::kLbPolicyDrop:
      return TYPE_URL(TYPE_INT_TAG "lb_policy_drop");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

const char* GetStatusStrPropertyUrl(StatusStrProperty key) {
  switch (key) {
    case StatusStrProperty::kDescription:
      return TYPE_URL(TYPE_STR_TAG "description");
    case StatusStrProperty::kFile:
      return TYPE_URL(TYPE_STR_TAG "file");
    case StatusStrProperty::kOsError:
      return TYPE_URL(TYPE_STR_TAG "os_error");
    case StatusStrProperty::kSyscall:
      return TYPE_URL(TYPE_STR_TAG "syscall");
    case StatusStrProperty::kTargetAddress:
      return TYPE_URL(TYPE_STR_TAG "target_address");
    case StatusStrProperty::kGrpcMessage:
      return TYPE_URL(TYPE_STR_TAG "grpc_message");
    case StatusStrProperty::kRawBytes:
      return TYPE_URL(TYPE_STR_TAG "raw_bytes");
    case StatusStrProperty::kTsiError:
      return TYPE_URL(TYPE_STR_TAG "tsi_error");
    case StatusStrProperty::kFilename:
      return TYPE_URL(TYPE_STR_TAG "filename");
    case StatusStrProperty::kKey:
      return TYPE_URL(TYPE_STR_TAG "key");
    case StatusStrProperty::kValue:
      return TYPE_URL(TYPE_STR_TAG "value");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

const char* GetStatusTimePropertyUrl(StatusTimeProperty key) {
  switch (key) {
    case StatusTimeProperty::kCreated:
      return TYPE_URL(TYPE_TIME_TAG "created_time");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

void EncodeUInt32ToBytes(uint32_t v, char* buf) {
  buf[0] = v & 0xFF;
  buf[1] = (v >> 8) & 0xFF;
  buf[2] = (v >> 16) & 0xFF;
  buf[3] = (v >> 24) & 0xFF;
}

uint32_t DecodeUInt32FromBytes(const char* buf) {
  return buf[0] | (uint32_t(buf[1]) << 8) | (uint32_t(buf[2]) << 16) |
         (uint32_t(buf[3]) << 24);
}

std::vector<absl::Status> ParseChildren(absl::Cord children) {
  std::vector<absl::Status> result;
  upb::Arena arena;
  // Cord is flattened to iterate the buffer easily at the cost of memory copy.
  // TODO(veblush): Optimize this once CordReader is introduced.
  absl::string_view buf = children.Flatten();
  size_t cur = 0;
  while (buf.size() - cur >= sizeof(uint32_t)) {
    size_t msg_size = DecodeUInt32FromBytes(buf.data() + cur);
    cur += sizeof(uint32_t);
    GPR_ASSERT(buf.size() - cur >= msg_size);
    google_rpc_Status* msg =
        google_rpc_Status_parse(buf.data() + cur, msg_size, arena.ptr());
    cur += msg_size;
    result.push_back(internal::StatusFromProto(msg));
  }
  return result;
}

}  // namespace

absl::Status StatusCreate(absl::StatusCode code, absl::string_view msg,
                          const DebugLocation& location,
                          std::initializer_list<absl::Status> children) {
  absl::Status s(code, msg);
  if (location.file() != nullptr) {
    StatusSetStr(&s, StatusStrProperty::kFile, location.file());
  }
  if (location.line() != -1) {
    StatusSetInt(&s, StatusIntProperty::kFileLine, location.line());
  }
  StatusSetTime(&s, StatusTimeProperty::kCreated, absl::Now());
  for (const absl::Status& child : children) {
    if (!child.ok()) {
      StatusAddChild(&s, child);
    }
  }
  return s;
}

void StatusSetInt(absl::Status* status, StatusIntProperty key, intptr_t value) {
  status->SetPayload(GetStatusIntPropertyUrl(key),
                     absl::Cord(std::to_string(value)));
}

absl::optional<intptr_t> StatusGetInt(const absl::Status& status,
                                      StatusIntProperty key) {
  absl::optional<absl::Cord> p =
      status.GetPayload(GetStatusIntPropertyUrl(key));
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

void StatusSetStr(absl::Status* status, StatusStrProperty key,
                  absl::string_view value) {
  status->SetPayload(GetStatusStrPropertyUrl(key), absl::Cord(value));
}

absl::optional<std::string> StatusGetStr(const absl::Status& status,
                                         StatusStrProperty key) {
  absl::optional<absl::Cord> p =
      status.GetPayload(GetStatusStrPropertyUrl(key));
  if (p.has_value()) {
    return std::string(*p);
  }
  return {};
}

void StatusSetTime(absl::Status* status, StatusTimeProperty key,
                   absl::Time time) {
  status->SetPayload(GetStatusTimePropertyUrl(key),
                     absl::Cord(absl::string_view(
                         reinterpret_cast<const char*>(&time), sizeof(time))));
}

absl::optional<absl::Time> StatusGetTime(const absl::Status& status,
                                         StatusTimeProperty key) {
  absl::optional<absl::Cord> p =
      status.GetPayload(GetStatusTimePropertyUrl(key));
  if (p.has_value()) {
    absl::optional<absl::string_view> sv = p->TryFlat();
    if (sv.has_value()) {
      return *reinterpret_cast<const absl::Time*>(sv->data());
    } else {
      std::string s = std::string(*p);
      return *reinterpret_cast<const absl::Time*>(s.c_str());
    }
  }
  return {};
}

void StatusAddChild(absl::Status* status, absl::Status child) {
  upb::Arena arena;
  // Serialize msg to buf
  google_rpc_Status* msg = internal::StatusToProto(child, arena.ptr());
  size_t buf_len = 0;
  char* buf = google_rpc_Status_serialize(msg, arena.ptr(), &buf_len);
  // Append (msg-length and msg) to children payload
  absl::optional<absl::Cord> old_children =
      status->GetPayload(kChildrenPropertyUrl);
  absl::Cord children;
  if (old_children.has_value()) {
    children = *old_children;
  }
  char head_buf[sizeof(uint32_t)];
  EncodeUInt32ToBytes(buf_len, head_buf);
  children.Append(absl::string_view(head_buf, sizeof(uint32_t)));
  children.Append(absl::string_view(buf, buf_len));
  status->SetPayload(kChildrenPropertyUrl, std::move(children));
}

std::vector<absl::Status> StatusGetChildren(absl::Status status) {
  absl::optional<absl::Cord> children = status.GetPayload(kChildrenPropertyUrl);
  return children.has_value() ? ParseChildren(*children)
                              : std::vector<absl::Status>();
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
    if (absl::StartsWith(type_url, kTypeUrlPrefix)) {
      type_url.remove_prefix(kTypeUrlPrefix.size());
      if (type_url == kTypeChildrenTag) {
        children = payload;
        return;
      }
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
      } else if (absl::StartsWith(type_url, kTypeStrTag)) {
        type_url.remove_prefix(kTypeStrTag.size());
        kvs.push_back(absl::StrCat(type_url, ":\"",
                                   absl::CHexEscape(payload_view), "\""));
      } else if (absl::StartsWith(type_url, kTypeTimeTag)) {
        type_url.remove_prefix(kTypeTimeTag.size());
        absl::Time t =
            *reinterpret_cast<const absl::Time*>(payload_view.data());
        kvs.push_back(absl::StrCat(type_url, ":\"", absl::FormatTime(t), "\""));
      } else {
        kvs.push_back(absl::StrCat(type_url, ":\"",
                                   absl::CHexEscape(payload_view), "\""));
      }
    } else {
      absl::optional<absl::string_view> payload_view = payload.TryFlat();
      std::string payload_str = absl::CHexEscape(
          payload_view.has_value() ? *payload_view : std::string(payload));
      kvs.push_back(absl::StrCat(type_url, ":\"", payload_str, "\""));
    }
  });
  if (children.has_value()) {
    std::vector<absl::Status> children_status = ParseChildren(*children);
    std::vector<std::string> children_text;
    children_text.reserve(children_status.size());
    for (const absl::Status& child_status : children_status) {
      children_text.push_back(StatusToString(child_status));
    }
    kvs.push_back(
        absl::StrCat("children:[", absl::StrJoin(children_text, ", "), "]"));
  }
  return kvs.empty() ? head
                     : absl::StrCat(head, " {", absl::StrJoin(kvs, ", "), "}");
}

namespace internal {

google_rpc_Status* StatusToProto(absl::Status status, upb_arena* arena) {
  google_rpc_Status* msg = google_rpc_Status_new(arena);
  google_rpc_Status_set_code(msg, int32_t(status.code()));
  google_rpc_Status_set_message(
      msg, upb_strview_make(status.message().data(), status.message().size()));
  status.ForEachPayload([&](absl::string_view type_url,
                            const absl::Cord& payload) {
    google_protobuf_Any* any = google_rpc_Status_add_details(msg, arena);
    char* type_url_buf =
        reinterpret_cast<char*>(upb_arena_malloc(arena, type_url.size()));
    memcpy(type_url_buf, type_url.data(), type_url.size());
    google_protobuf_Any_set_type_url(
        any, upb_strview_make(type_url_buf, type_url.size()));
    absl::optional<absl::string_view> v_view = payload.TryFlat();
    if (v_view.has_value()) {
      google_protobuf_Any_set_value(
          any, upb_strview_make(v_view->data(), v_view->size()));
    } else {
      char* buf =
          reinterpret_cast<char*>(upb_arena_malloc(arena, payload.size()));
      char* cur = buf;
      for (absl::string_view chunk : payload.Chunks()) {
        memcpy(cur, chunk.data(), chunk.size());
        cur += chunk.size();
      }
      google_protobuf_Any_set_value(any, upb_strview_make(buf, payload.size()));
    }
  });
  return msg;
}

absl::Status StatusFromProto(google_rpc_Status* msg) {
  int32_t code = google_rpc_Status_code(msg);
  upb_strview message = google_rpc_Status_message(msg);
  absl::Status status(static_cast<absl::StatusCode>(code),
                      absl::string_view(message.data, message.size));
  size_t detail_len;
  const google_protobuf_Any* const* details =
      google_rpc_Status_details(msg, &detail_len);
  for (size_t i = 0; i < detail_len; i++) {
    upb_strview type_url = google_protobuf_Any_type_url(details[i]);
    upb_strview value = google_protobuf_Any_value(details[i]);
    status.SetPayload(absl::string_view(type_url.data, type_url.size),
                      absl::Cord(absl::string_view(value.data, value.size)));
  }
  return status;
}

uintptr_t StatusAllocPtr(absl::Status s) {
  // This relies the fact that absl::Status has only one member, StatusRep*
  // so the sizeof(absl::Status) has the same size of intptr_t and StatusRep*
  // can be stolen using placement allocation.
  static_assert(sizeof(intptr_t) == sizeof(absl::Status),
                "absl::Status should be as big as intptr_t");
  // This does two things;
  // 1. Copies StatusRep* of absl::Status to ptr
  // 2. Increases the counter of StatusRep if it's not inlined
  uintptr_t ptr;
  new (&ptr) absl::Status(s);
  return ptr;
}

void StatusFreePtr(uintptr_t ptr) {
  // Decreases the counter of StatusRep if it's not inlined.
  reinterpret_cast<absl::Status*>(&ptr)->~Status();
}

absl::Status StatusGetFromPtr(uintptr_t ptr) {
  // Constructs Status from ptr having the address of StatusRep.
  return *reinterpret_cast<absl::Status*>(&ptr);
}

}  // namespace internal

}  // namespace grpc_core
