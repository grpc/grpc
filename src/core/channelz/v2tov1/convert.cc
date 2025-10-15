// Copyright 2025 gRPC authors.
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

#include "src/core/channelz/v2tov1/convert.h"

#include <optional>
#include <string>
#include <vector>

#include "google/protobuf/any.upb.h"
#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/protobuf/wrappers.upb.h"
#include "src/core/channelz/v2tov1/property_list.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/uri.h"
#include "src/proto/grpc/channelz/channelz.upb.h"
#include "src/proto/grpc/channelz/channelz.upbdefs.h"
#include "src/proto/grpc/channelz/v2/channelz.upb.h"
#include "src/proto/grpc/channelz/v2/property_list.upb.h"
#include "upb/base/status.h"
#include "upb/json/encode.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"
#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

namespace grpc_core {
namespace channelz {
namespace v2tov1 {

namespace {
const grpc_channelz_v2_Data* FindData(const grpc_channelz_v2_Entity* entity,
                                      absl::string_view name) {
  size_t num_data;
  const grpc_channelz_v2_Data* const* data =
      grpc_channelz_v2_Entity_data(entity, &num_data);
  for (size_t i = 0; i < num_data; ++i) {
    upb_StringView data_name = grpc_channelz_v2_Data_name(data[i]);
    if (absl::string_view(data_name.data, data_name.size) == name) {
      return data[i];
    }
  }
  return nullptr;
}

const grpc_channelz_v2_PropertyList* GetPropertyList(
    const grpc_channelz_v2_Entity* entity, absl::string_view name,
    upb_Arena* arena) {
  const auto* data = FindData(entity, name);
  if (data == nullptr) return nullptr;
  const auto* value = grpc_channelz_v2_Data_value(data);
  if (value == nullptr) return nullptr;
  upb_StringView type_url = google_protobuf_Any_type_url(value);
  if (absl::string_view(type_url.data, type_url.size) !=
      "type.googleapis.com/grpc.channelz.v2.PropertyList") {
    return nullptr;
  }
  upb_StringView bytes = google_protobuf_Any_value(value);
  return grpc_channelz_v2_PropertyList_parse(bytes.data, bytes.size, arena);
}

void ParseAddress(const std::string& addr_str, upb_Arena* arena,
                  grpc_channelz_v1_Address* address) {
  absl::StatusOr<URI> uri = URI::Parse(addr_str);
  if (!uri.ok()) {
    grpc_channelz_v1_Address_OtherAddress_set_name(
        grpc_channelz_v1_Address_mutable_other_address(address, arena),
        CopyStdStringToUpbString(addr_str, arena));
    return;
  }
  if (uri->scheme() == "ipv4" || uri->scheme() == "ipv6") {
    absl::StatusOr<grpc_resolved_address> resolved_address =
        StringToSockaddr(absl::StripPrefix(uri->path(), "/"));
    if (resolved_address.ok()) {
      auto* tcpip_address =
          grpc_channelz_v1_Address_mutable_tcpip_address(address, arena);
      grpc_channelz_v1_Address_TcpIpAddress_set_port(
          tcpip_address, grpc_sockaddr_get_port(&*resolved_address));
      grpc_channelz_v1_Address_TcpIpAddress_set_ip_address(
          tcpip_address,
          CopyStdStringToUpbString(
              grpc_sockaddr_get_packed_host(&*resolved_address), arena));
      return;
    }
  } else if (uri->scheme() == "unix") {
    grpc_channelz_v1_Address_UdsAddress_set_filename(
        grpc_channelz_v1_Address_mutable_uds_address(address, arena),
        CopyStdStringToUpbString(std::string(uri->path()), arena));
    return;
  }
  grpc_channelz_v1_Address_OtherAddress_set_name(
      grpc_channelz_v1_Address_mutable_other_address(address, arena),
      CopyStdStringToUpbString(addr_str, arena));
}

absl::StatusOr<std::string> SerializeJson(
    const upb_Message* msg,
    const upb_MessageDef* (*msg_def_maker)(upb_DefPool*)) {
  upb::DefPool pool;
  auto* msg_def = msg_def_maker(pool.ptr());
  upb_Status status;
  upb_Status_Clear(&status);
  size_t len = upb_JsonEncode(msg, msg_def, pool.ptr(),
                              upb_JsonEncode_EmitDefaults, nullptr, 0, &status);
  if (!upb_Status_IsOk(&status)) {
    return absl::InternalError(absl::StrCat("Failed to serialize to JSON: ",
                                            upb_Status_ErrorMessage(&status)));
  }
  auto buf = std::make_unique<char[]>(len + 1);
  upb_JsonEncode(msg, msg_def, pool.ptr(), upb_JsonEncode_EmitDefaults,
                 buf.get(), len + 1, &status);
  if (!upb_Status_IsOk(&status)) {
    return absl::InternalError(absl::StrCat("Failed to serialize to JSON: ",
                                            upb_Status_ErrorMessage(&status)));
  }
  return std::string(buf.get(), len);
}

grpc_channelz_v1_ChannelConnectivityState_State ConnectivityStateFromString(
    absl::string_view state) {
  if (state == "READY") return grpc_channelz_v1_ChannelConnectivityState_READY;
  if (state == "IDLE") return grpc_channelz_v1_ChannelConnectivityState_IDLE;
  if (state == "CONNECTING") {
    return grpc_channelz_v1_ChannelConnectivityState_CONNECTING;
  }
  if (state == "TRANSIENT_FAILURE") {
    return grpc_channelz_v1_ChannelConnectivityState_TRANSIENT_FAILURE;
  }
  if (state == "SHUTDOWN") {
    return grpc_channelz_v1_ChannelConnectivityState_SHUTDOWN;
  }
  return grpc_channelz_v1_ChannelConnectivityState_UNKNOWN;
}

void PopulateV1Trace(const grpc_channelz_v2_TraceEvent* const* trace_events,
                     size_t num_events, upb_Arena* arena,
                     grpc_channelz_v1_ChannelTrace* trace) {
  if (num_events > 0) {
    const auto* ts = grpc_channelz_v2_TraceEvent_timestamp(trace_events[0]);
    if (ts != nullptr) {
      auto* v1_ts = grpc_channelz_v1_ChannelTrace_mutable_creation_timestamp(
          trace, arena);
      google_protobuf_Timestamp_set_seconds(
          v1_ts, google_protobuf_Timestamp_seconds(ts));
      google_protobuf_Timestamp_set_nanos(v1_ts,
                                          google_protobuf_Timestamp_nanos(ts));
    }
  }
  for (size_t i = 0; i < num_events; ++i) {
    auto* v1_event = grpc_channelz_v1_ChannelTrace_add_events(trace, arena);
    upb_StringView description =
        grpc_channelz_v2_TraceEvent_description(trace_events[i]);
    grpc_channelz_v1_ChannelTraceEvent_set_description(v1_event, description);
    const auto* ts = grpc_channelz_v2_TraceEvent_timestamp(trace_events[i]);
    if (ts != nullptr) {
      auto* v1_ts =
          grpc_channelz_v1_ChannelTraceEvent_mutable_timestamp(v1_event, arena);
      google_protobuf_Timestamp_set_seconds(
          v1_ts, google_protobuf_Timestamp_seconds(ts));
      google_protobuf_Timestamp_set_nanos(v1_ts,
                                          google_protobuf_Timestamp_nanos(ts));
    }
    grpc_channelz_v1_ChannelTraceEvent_set_severity(
        v1_event, grpc_channelz_v1_ChannelTraceEvent_CT_INFO);
  }
}

void PopulateV1ChannelData(
    const grpc_channelz_v2_PropertyList* channel_props,
    const grpc_channelz_v2_PropertyList* call_counts,
    const grpc_channelz_v2_TraceEvent* const* trace_events, size_t num_events,
    upb_Arena* arena, grpc_channelz_v1_ChannelData* data) {
  if (channel_props) {
    if (auto target = StringFromPropertyList(channel_props, "target");
        target.has_value()) {
      grpc_channelz_v1_ChannelData_set_target(
          data, CopyStdStringToUpbString(*target, arena));
    }
    if (auto state =
            StringFromPropertyList(channel_props, "connectivity_state");
        state.has_value()) {
      grpc_channelz_v1_ChannelConnectivityState_set_state(
          grpc_channelz_v1_ChannelData_mutable_state(data, arena),
          ConnectivityStateFromString(*state));
    }
  }
  if (call_counts != nullptr) {
    grpc_channelz_v1_ChannelData_set_calls_started(
        data, Int64FromPropertyList(call_counts, "calls_started").value_or(0));
    grpc_channelz_v1_ChannelData_set_calls_succeeded(
        data,
        Int64FromPropertyList(call_counts, "calls_succeeded").value_or(0));
    grpc_channelz_v1_ChannelData_set_calls_failed(
        data, Int64FromPropertyList(call_counts, "calls_failed").value_or(0));
    if (auto* last_call_started_timestamp = TimestampFromPropertyList(
            call_counts, "last_call_started_timestamp");
        last_call_started_timestamp != nullptr) {
      auto* v1_ts =
          grpc_channelz_v1_ChannelData_mutable_last_call_started_timestamp(
              data, arena);
      google_protobuf_Timestamp_set_seconds(
          v1_ts,
          google_protobuf_Timestamp_seconds(last_call_started_timestamp));
      google_protobuf_Timestamp_set_nanos(
          v1_ts, google_protobuf_Timestamp_nanos(last_call_started_timestamp));
    }
  }
  if (num_events > 0) {
    auto* trace = grpc_channelz_v1_ChannelData_mutable_trace(data, arena);
    PopulateV1Trace(trace_events, num_events, arena, trace);
  }
}

}  // namespace

absl::StatusOr<std::string> ConvertServer(const std::string& serialized_entity,
                                          EntityFetcher& fetcher, bool json) {
  upb::Arena arena;
  const auto* entity = grpc_channelz_v2_Entity_parse(
      serialized_entity.data(), serialized_entity.size(), arena.ptr());
  if (entity == nullptr) {
    return absl::InvalidArgumentError("Failed to parse entity");
  }

  upb_StringView kind = grpc_channelz_v2_Entity_kind(entity);
  if (absl::string_view(kind.data, kind.size) != "server") {
    return absl::InvalidArgumentError("Entity kind is not server");
  }

  grpc_channelz_v1_Server* v1 = grpc_channelz_v1_Server_new(arena.ptr());
  grpc_channelz_v1_ServerRef_set_server_id(
      grpc_channelz_v1_Server_mutable_ref(v1, arena.ptr()),
      grpc_channelz_v2_Entity_id(entity));
  const auto* call_counts = GetPropertyList(entity, "call_counts", arena.ptr());
  size_t num_events;
  const auto* trace_events = grpc_channelz_v2_Entity_trace(entity, &num_events);
  if (call_counts != nullptr || num_events > 0) {
    auto* data = grpc_channelz_v1_Server_mutable_data(v1, arena.ptr());
    if (call_counts != nullptr) {
      grpc_channelz_v1_ServerData_set_calls_started(
          data,
          Int64FromPropertyList(call_counts, "calls_started").value_or(0));
      grpc_channelz_v1_ServerData_set_calls_succeeded(
          data,
          Int64FromPropertyList(call_counts, "calls_succeeded").value_or(0));
      grpc_channelz_v1_ServerData_set_calls_failed(
          data, Int64FromPropertyList(call_counts, "calls_failed").value_or(0));
      if (auto* last_call_started_timestamp = TimestampFromPropertyList(
              call_counts, "last_call_started_timestamp")) {
        auto* v1_ts =
            grpc_channelz_v1_ServerData_mutable_last_call_started_timestamp(
                data, arena.ptr());
        google_protobuf_Timestamp_set_seconds(
            v1_ts,
            google_protobuf_Timestamp_seconds(last_call_started_timestamp));
        google_protobuf_Timestamp_set_nanos(
            v1_ts,
            google_protobuf_Timestamp_nanos(last_call_started_timestamp));
      }
    }
    if (num_events > 0) {
      auto* trace =
          grpc_channelz_v1_ServerData_mutable_trace(data, arena.ptr());
      PopulateV1Trace(trace_events, num_events, arena.ptr(), trace);
    }
  }
  auto children =
      fetcher.GetEntitiesWithParent(grpc_channelz_v2_Entity_id(entity));
  if (children.ok()) {
    for (const auto& child_str : *children) {
      const auto* child_entity = grpc_channelz_v2_Entity_parse(
          child_str.data(), child_str.size(), arena.ptr());
      if (child_entity == nullptr) continue;
      upb_StringView kind = grpc_channelz_v2_Entity_kind(child_entity);
      if (absl::string_view(kind.data, kind.size) != "listen_socket") {
        continue;
      }
      auto* added = grpc_channelz_v1_Server_add_listen_socket(v1, arena.ptr());
      grpc_channelz_v1_SocketRef_set_socket_id(
          added, grpc_channelz_v2_Entity_id(child_entity));
      if (auto v1_compat =
              GetPropertyList(child_entity, "v1_compatibility", arena.ptr());
          v1_compat != nullptr) {
        if (auto name = StringFromPropertyList(v1_compat, "name");
            name.has_value()) {
          grpc_channelz_v1_SocketRef_set_name(
              added, CopyStdStringToUpbString(*name, arena.ptr()));
        }
      }
    }
  }
  if (json) {
    return SerializeJson((upb_Message*)v1, grpc_channelz_v1_Server_getmsgdef);
  }
  size_t length;
  char* bytes = grpc_channelz_v1_Server_serialize(v1, arena.ptr(), &length);
  return std::string(bytes, length);
}

absl::StatusOr<std::string> ConvertSocket(const std::string& serialized_entity,
                                          EntityFetcher&, bool json) {
  upb::Arena arena;
  const auto* entity = grpc_channelz_v2_Entity_parse(
      serialized_entity.data(), serialized_entity.size(), arena.ptr());
  if (entity == nullptr) {
    return absl::InvalidArgumentError("Failed to parse entity");
  }
  upb_StringView kind = grpc_channelz_v2_Entity_kind(entity);
  if (absl::string_view(kind.data, kind.size) != "socket") {
    return absl::InvalidArgumentError("Entity kind is not socket");
  }

  grpc_channelz_v1_Socket* v1 = grpc_channelz_v1_Socket_new(arena.ptr());
  auto* v1_ref = grpc_channelz_v1_Socket_mutable_ref(v1, arena.ptr());
  grpc_channelz_v1_SocketRef_set_socket_id(v1_ref,
                                           grpc_channelz_v2_Entity_id(entity));
  if (auto v1_compat = GetPropertyList(entity, "v1_compatibility", arena.ptr());
      v1_compat != nullptr) {
    if (auto name = StringFromPropertyList(v1_compat, "name");
        name.has_value()) {
      grpc_channelz_v1_SocketRef_set_name(
          v1_ref, CopyStdStringToUpbString(*name, arena.ptr()));
    }
  }
  const auto* call_counts = GetPropertyList(entity, "call_counts", arena.ptr());
  const auto* http2 = GetPropertyList(entity, "http2", arena.ptr());
  if (call_counts != nullptr || http2 != nullptr) {
    auto* data = grpc_channelz_v1_Socket_mutable_data(v1, arena.ptr());
    if (call_counts != nullptr) {
      if (auto streams_started =
              Int64FromPropertyList(call_counts, "streams_started");
          streams_started.has_value()) {
        grpc_channelz_v1_SocketData_set_streams_started(data, *streams_started);
      }
      if (auto streams_succeeded =
              Int64FromPropertyList(call_counts, "streams_succeeded");
          streams_succeeded.has_value()) {
        grpc_channelz_v1_SocketData_set_streams_succeeded(data,
                                                          *streams_succeeded);
      }
      if (auto streams_failed =
              Int64FromPropertyList(call_counts, "streams_failed");
          streams_failed.has_value()) {
        grpc_channelz_v1_SocketData_set_streams_failed(data, *streams_failed);
      }
      if (auto messages_sent =
              Int64FromPropertyList(call_counts, "messages_sent");
          messages_sent.has_value()) {
        grpc_channelz_v1_SocketData_set_messages_sent(data, *messages_sent);
      }
      if (auto messages_received =
              Int64FromPropertyList(call_counts, "messages_received");
          messages_received.has_value()) {
        grpc_channelz_v1_SocketData_set_messages_received(data,
                                                          *messages_received);
      }
      if (auto keepalives_sent =
              Int64FromPropertyList(call_counts, "keepalives_sent");
          keepalives_sent.has_value()) {
        grpc_channelz_v1_SocketData_set_keep_alives_sent(data,
                                                         *keepalives_sent);
      }
      if (auto* last_local_stream_created_timestamp = TimestampFromPropertyList(
              call_counts, "last_local_stream_created_timestamp");
          last_local_stream_created_timestamp != nullptr) {
        auto* v1_ts =
            grpc_channelz_v1_SocketData_mutable_last_local_stream_created_timestamp(
                data, arena.ptr());
        google_protobuf_Timestamp_set_seconds(
            v1_ts, google_protobuf_Timestamp_seconds(
                       last_local_stream_created_timestamp));
        google_protobuf_Timestamp_set_nanos(
            v1_ts, google_protobuf_Timestamp_nanos(
                       last_local_stream_created_timestamp));
      }
      if (auto* last_remote_stream_created_timestamp =
              TimestampFromPropertyList(call_counts,
                                        "last_remote_stream_created_timestamp");
          last_remote_stream_created_timestamp != nullptr) {
        auto* v1_ts =
            grpc_channelz_v1_SocketData_mutable_last_remote_stream_created_timestamp(
                data, arena.ptr());
        google_protobuf_Timestamp_set_seconds(
            v1_ts, google_protobuf_Timestamp_seconds(
                       last_remote_stream_created_timestamp));
        google_protobuf_Timestamp_set_nanos(
            v1_ts, google_protobuf_Timestamp_nanos(
                       last_remote_stream_created_timestamp));
      }
      if (auto* last_message_sent_timestamp = TimestampFromPropertyList(
              call_counts, "last_message_sent_timestamp");
          last_message_sent_timestamp != nullptr) {
        auto* v1_ts =
            grpc_channelz_v1_SocketData_mutable_last_message_sent_timestamp(
                data, arena.ptr());
        google_protobuf_Timestamp_set_seconds(
            v1_ts,
            google_protobuf_Timestamp_seconds(last_message_sent_timestamp));
        google_protobuf_Timestamp_set_nanos(
            v1_ts,
            google_protobuf_Timestamp_nanos(last_message_sent_timestamp));
      }
      if (auto* last_message_received_timestamp = TimestampFromPropertyList(
              call_counts, "last_message_received_timestamp");
          last_message_received_timestamp != nullptr) {
        auto* v1_ts =
            grpc_channelz_v1_SocketData_mutable_last_message_received_timestamp(
                data, arena.ptr());
        google_protobuf_Timestamp_set_seconds(
            v1_ts,
            google_protobuf_Timestamp_seconds(last_message_received_timestamp));
        google_protobuf_Timestamp_set_nanos(
            v1_ts,
            google_protobuf_Timestamp_nanos(last_message_received_timestamp));
      }
    }
    if (http2 != nullptr) {
      if (auto flow_control =
              PropertyListFromPropertyList(http2, "flow_control", arena.ptr());
          flow_control != nullptr) {
        if (auto remote_window =
                Int64FromPropertyList(flow_control, "remote_window");
            remote_window.has_value()) {
          google_protobuf_Int64Value_set_value(
              grpc_channelz_v1_SocketData_mutable_local_flow_control_window(
                  data, arena.ptr()),
              *remote_window);
        }
        if (auto announced_window =
                Int64FromPropertyList(flow_control, "announced_window");
            announced_window.has_value()) {
          google_protobuf_Int64Value_set_value(
              grpc_channelz_v1_SocketData_mutable_remote_flow_control_window(
                  data, arena.ptr()),
              *announced_window);
        }
      }
    }
  }

  if (auto socket_props = GetPropertyList(entity, "socket", arena.ptr());
      socket_props != nullptr) {
    if (auto local = StringFromPropertyList(socket_props, "local");
        local.has_value()) {
      ParseAddress(*local, arena.ptr(),
                   grpc_channelz_v1_Socket_mutable_local(v1, arena.ptr()));
    }
    if (auto remote = StringFromPropertyList(socket_props, "remote");
        remote.has_value()) {
      ParseAddress(*remote, arena.ptr(),
                   grpc_channelz_v1_Socket_mutable_remote(v1, arena.ptr()));
    }
  }

  if (auto security = GetPropertyList(entity, "security", arena.ptr());
      security != nullptr) {
    auto* v1_security =
        grpc_channelz_v1_Socket_mutable_security(v1, arena.ptr());
    if (auto other = StringFromPropertyList(security, "other");
        other.has_value()) {
      grpc_channelz_v1_Security_OtherSecurity_set_name(
          grpc_channelz_v1_Security_mutable_other(v1_security, arena.ptr()),
          CopyStdStringToUpbString(*other, arena.ptr()));
    } else {
      auto* tls =
          grpc_channelz_v1_Security_mutable_tls(v1_security, arena.ptr());
      if (auto standard_name =
              StringFromPropertyList(security, "standard_name");
          standard_name.has_value()) {
        grpc_channelz_v1_Security_Tls_set_standard_name(
            tls, CopyStdStringToUpbString(*standard_name, arena.ptr()));
      }
      if (auto other_name = StringFromPropertyList(security, "other_name");
          other_name.has_value()) {
        grpc_channelz_v1_Security_Tls_set_other_name(
            tls, CopyStdStringToUpbString(*other_name, arena.ptr()));
      }
      if (auto local_cert =
              StringFromPropertyList(security, "local_certificate");
          local_cert.has_value()) {
        std::string decoded;
        if (absl::Base64Unescape(*local_cert, &decoded)) {
          grpc_channelz_v1_Security_Tls_set_local_certificate(
              tls, CopyStdStringToUpbString(decoded, arena.ptr()));
        }
      }
      if (auto remote_cert =
              StringFromPropertyList(security, "remote_certificate");
          remote_cert.has_value()) {
        std::string decoded;
        if (absl::Base64Unescape(*remote_cert, &decoded)) {
          grpc_channelz_v1_Security_Tls_set_remote_certificate(
              tls, CopyStdStringToUpbString(decoded, arena.ptr()));
        }
      }
    }
  }

  if (json) {
    return SerializeJson((upb_Message*)v1, grpc_channelz_v1_Socket_getmsgdef);
  }
  size_t length;
  char* bytes = grpc_channelz_v1_Socket_serialize(v1, arena.ptr(), &length);
  return std::string(bytes, length);
}

absl::StatusOr<std::string> ConvertChannel(const std::string& serialized_entity,
                                           EntityFetcher& fetcher, bool json) {
  upb::Arena arena;
  const auto* entity = grpc_channelz_v2_Entity_parse(
      serialized_entity.data(), serialized_entity.size(), arena.ptr());
  if (entity == nullptr) {
    return absl::InvalidArgumentError("Failed to parse entity");
  }
  upb_StringView kind = grpc_channelz_v2_Entity_kind(entity);
  if (absl::string_view(kind.data, kind.size) != "channel" &&
      absl::string_view(kind.data, kind.size) != "top_level_channel") {
    return absl::InvalidArgumentError(
        "Entity kind is not channel or top_level_channel");
  }
  grpc_channelz_v1_Channel* v1 = grpc_channelz_v1_Channel_new(arena.ptr());
  grpc_channelz_v1_ChannelRef_set_channel_id(
      grpc_channelz_v1_Channel_mutable_ref(v1, arena.ptr()),
      grpc_channelz_v2_Entity_id(entity));
  const auto* channel_props = GetPropertyList(entity, "channel", arena.ptr());
  const auto* call_counts = GetPropertyList(entity, "call_counts", arena.ptr());
  size_t num_events;
  const auto* trace_events = grpc_channelz_v2_Entity_trace(entity, &num_events);
  auto* data = grpc_channelz_v1_Channel_mutable_data(v1, arena.ptr());
  PopulateV1ChannelData(channel_props, call_counts, trace_events, num_events,
                        arena.ptr(), data);
  auto children =
      fetcher.GetEntitiesWithParent(grpc_channelz_v2_Entity_id(entity));
  if (children.ok()) {
    for (const auto& child_str : *children) {
      const auto* child = grpc_channelz_v2_Entity_parse(
          child_str.data(), child_str.size(), arena.ptr());
      if (child == nullptr) continue;
      upb_StringView child_kind = grpc_channelz_v2_Entity_kind(child);
      if (absl::string_view(child_kind.data, child_kind.size) == "channel") {
        auto* child_ref =
            grpc_channelz_v1_Channel_add_channel_ref(v1, arena.ptr());
        grpc_channelz_v1_ChannelRef_set_channel_id(
            child_ref, grpc_channelz_v2_Entity_id(child));
      } else if (absl::string_view(child_kind.data, child_kind.size) ==
                 "subchannel") {
        auto* child_ref =
            grpc_channelz_v1_Channel_add_subchannel_ref(v1, arena.ptr());
        grpc_channelz_v1_SubchannelRef_set_subchannel_id(
            child_ref, grpc_channelz_v2_Entity_id(child));
      }
    }
  }
  if (json) {
    return SerializeJson((upb_Message*)v1, grpc_channelz_v1_Channel_getmsgdef);
  }
  size_t length;
  char* bytes = grpc_channelz_v1_Channel_serialize(v1, arena.ptr(), &length);
  return std::string(bytes, length);
}

absl::StatusOr<std::string> ConvertSubchannel(
    const std::string& serialized_entity, EntityFetcher& fetcher, bool json) {
  upb::Arena arena;
  const auto* entity = grpc_channelz_v2_Entity_parse(
      serialized_entity.data(), serialized_entity.size(), arena.ptr());
  if (entity == nullptr) {
    return absl::InvalidArgumentError("Failed to parse entity");
  }
  upb_StringView kind = grpc_channelz_v2_Entity_kind(entity);
  if (absl::string_view(kind.data, kind.size) != "subchannel") {
    return absl::InvalidArgumentError("Entity kind is not subchannel");
  }
  grpc_channelz_v1_Subchannel* v1 =
      grpc_channelz_v1_Subchannel_new(arena.ptr());
  grpc_channelz_v1_SubchannelRef_set_subchannel_id(
      grpc_channelz_v1_Subchannel_mutable_ref(v1, arena.ptr()),
      grpc_channelz_v2_Entity_id(entity));
  const auto* channel_props = GetPropertyList(entity, "channel", arena.ptr());
  const auto* call_counts = GetPropertyList(entity, "call_counts", arena.ptr());
  size_t num_events;
  const auto* trace_events = grpc_channelz_v2_Entity_trace(entity, &num_events);
  auto* data = grpc_channelz_v1_Subchannel_mutable_data(v1, arena.ptr());
  PopulateV1ChannelData(channel_props, call_counts, trace_events, num_events,
                        arena.ptr(), data);
  auto children =
      fetcher.GetEntitiesWithParent(grpc_channelz_v2_Entity_id(entity));
  if (children.ok()) {
    for (const auto& child_str : *children) {
      const auto* child = grpc_channelz_v2_Entity_parse(
          child_str.data(), child_str.size(), arena.ptr());
      if (child == nullptr) continue;
      upb_StringView child_kind = grpc_channelz_v2_Entity_kind(child);
      if (absl::string_view(child_kind.data, child_kind.size) == "channel") {
        auto* child_ref =
            grpc_channelz_v1_Subchannel_add_channel_ref(v1, arena.ptr());
        grpc_channelz_v1_ChannelRef_set_channel_id(
            child_ref, grpc_channelz_v2_Entity_id(child));
      } else if (absl::string_view(child_kind.data, child_kind.size) ==
                 "subchannel") {
        auto* child_ref =
            grpc_channelz_v1_Subchannel_add_subchannel_ref(v1, arena.ptr());
        grpc_channelz_v1_SubchannelRef_set_subchannel_id(
            child_ref, grpc_channelz_v2_Entity_id(child));
      } else if (absl::string_view(child_kind.data, child_kind.size) ==
                 "socket") {
        auto* child_ref =
            grpc_channelz_v1_Subchannel_add_socket_ref(v1, arena.ptr());
        grpc_channelz_v1_SocketRef_set_socket_id(
            child_ref, grpc_channelz_v2_Entity_id(child));
        if (auto v1_compat =
                GetPropertyList(child, "v1_compatibility", arena.ptr());
            v1_compat != nullptr) {
          if (auto name = StringFromPropertyList(v1_compat, "name");
              name.has_value()) {
            grpc_channelz_v1_SocketRef_set_name(
                child_ref, CopyStdStringToUpbString(*name, arena.ptr()));
          }
        }
      }
    }
  }
  if (json) {
    return SerializeJson((upb_Message*)v1,
                         grpc_channelz_v1_Subchannel_getmsgdef);
  }
  size_t length;
  char* bytes = grpc_channelz_v1_Subchannel_serialize(v1, arena.ptr(), &length);
  return std::string(bytes, length);
}

absl::StatusOr<std::string> ConvertListenSocket(
    const std::string& serialized_entity, EntityFetcher&, bool json) {
  upb::Arena arena;
  const auto* entity = grpc_channelz_v2_Entity_parse(
      serialized_entity.data(), serialized_entity.size(), arena.ptr());
  if (entity == nullptr) {
    return absl::InvalidArgumentError("Failed to parse entity");
  }
  upb_StringView kind = grpc_channelz_v2_Entity_kind(entity);
  if (absl::string_view(kind.data, kind.size) != "listen_socket") {
    return absl::InvalidArgumentError("Entity kind is not listen_socket");
  }
  grpc_channelz_v1_Socket* v1 = grpc_channelz_v1_Socket_new(arena.ptr());
  auto* v1_ref = grpc_channelz_v1_Socket_mutable_ref(v1, arena.ptr());
  grpc_channelz_v1_SocketRef_set_socket_id(v1_ref,
                                           grpc_channelz_v2_Entity_id(entity));
  if (auto v1_compat = GetPropertyList(entity, "v1_compatibility", arena.ptr());
      v1_compat != nullptr) {
    if (auto name = StringFromPropertyList(v1_compat, "name");
        name.has_value()) {
      grpc_channelz_v1_SocketRef_set_name(
          v1_ref, CopyStdStringToUpbString(*name, arena.ptr()));
    }
  }
  if (auto socket_props = GetPropertyList(entity, "socket", arena.ptr());
      socket_props != nullptr) {
    if (auto local = StringFromPropertyList(socket_props, "local");
        local.has_value()) {
      ParseAddress(*local, arena.ptr(),
                   grpc_channelz_v1_Socket_mutable_local(v1, arena.ptr()));
    }
  }
  if (json) {
    return SerializeJson((upb_Message*)v1, grpc_channelz_v1_Socket_getmsgdef);
  }
  size_t length;
  char* bytes = grpc_channelz_v1_Socket_serialize(v1, arena.ptr(), &length);
  return std::string(bytes, length);
}

}  // namespace v2tov1
}  // namespace channelz
}  // namespace grpc_core
