//
//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/logging/logging_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/client_channel/client_channel_filter.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/logging/logging_sink.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/telemetry/call_tracer.h"
#include "src/core/util/host_port.h"
#include "src/core/util/latent_see.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"

namespace grpc_core {

namespace {

LoggingSink* g_logging_sink = nullptr;

absl::uint128 GetCallId() {
  thread_local absl::InsecureBitGen gen;
  return absl::uniform_int_distribution<absl::uint128>()(gen);
}

class MetadataEncoder {
 public:
  MetadataEncoder(LoggingSink::Entry::Payload* payload,
                  std::string* status_details_bin, uint64_t log_len)
      : payload_(payload),
        status_details_bin_(status_details_bin),
        log_len_(log_len) {}

  void Encode(const Slice& key_slice, const Slice& value_slice) {
    auto key = key_slice.as_string_view();
    auto value = value_slice.as_string_view();
    if (status_details_bin_ != nullptr && key == "grpc-status-details-bin") {
      *status_details_bin_ = std::string(value);
      return;
    }
    if (absl::ConsumePrefix(&key, "grpc-")) {
      // skip all other grpc- headers
      return;
    }
    uint64_t mdentry_len = key.length() + value.length();
    if (mdentry_len > log_len_) {
      VLOG(2) << "Skipped metadata key because of max metadata logging bytes "
              << mdentry_len << " (current) vs " << log_len_
              << " (max less already accounted metadata)";
      truncated_ = true;
      return;
    }

    payload_->metadata.emplace(std::string(key), std::string(value));
    log_len_ -= mdentry_len;
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType&) {}

  void Encode(GrpcStatusMetadata, grpc_status_code status) {
    payload_->status_code = status;
  }

  void Encode(GrpcMessageMetadata, const Slice& status_message) {
    payload_->status_message = std::string(status_message.as_string_view());
  }

  bool truncated() const { return truncated_; }

 private:
  LoggingSink::Entry::Payload* const payload_;
  std::string* const status_details_bin_;
  uint64_t log_len_;
  bool truncated_ = false;
};

void SetIpPort(absl::string_view s, LoggingSink::Entry::Address* peer) {
  absl::string_view host;
  absl::string_view port;
  if (SplitHostPort(absl::string_view(s.data(), s.length()), &host, &port) ==
      1) {
    if (!host.empty()) {
      peer->address = std::string(host);
    }
    if (!port.empty()) {
      if (!absl::SimpleAtoi(absl::string_view(port.data(), port.size()),
                            &peer->ip_port)) {
        peer->ip_port = 0;
      }
    }
  }
}

LoggingSink::Entry::Address PeerStringToAddress(const Slice& peer_string) {
  LoggingSink::Entry::Address address;
  absl::StatusOr<URI> uri = URI::Parse(peer_string.as_string_view());
  if (!uri.ok()) {
    VLOG(2) << "peer_string is in invalid format and cannot be logged";
    return address;
  }

  if (uri->scheme() == "ipv4") {
    address.type = LoggingSink::Entry::Address::Type::kIpv4;
    SetIpPort(uri->path(), &address);
  } else if (uri->scheme() == "ipv6") {
    address.type = LoggingSink::Entry::Address::Type::kIpv6;
    // TODO(zpencer): per grfc, verify RFC5952 section 4 styled addrs in use
    SetIpPort(uri->path(), &address);
  } else if (uri->scheme() == "unix") {
    address.type = LoggingSink::Entry::Address::Type::kUnix;
    address.address = uri->path();
  }
  return address;
}

void EncodeMessageToPayload(const SliceBuffer* message, uint32_t log_len,
                            LoggingSink::Entry* entry) {
  auto* sb = message->c_slice_buffer();
  entry->payload.message_length = sb->length;
  // Log the message to a max of the configured message length
  for (size_t i = 0; i < sb->count; i++) {
    absl::StrAppend(
        &entry->payload.message,
        absl::string_view(
            reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(sb->slices[i])),
            std::min(static_cast<size_t>(GRPC_SLICE_LENGTH(sb->slices[i])),
                     static_cast<size_t>(log_len))));
    if (log_len < GRPC_SLICE_LENGTH(sb->slices[i])) {
      entry->payload_truncated = true;
      break;
    }
    log_len -= GRPC_SLICE_LENGTH(sb->slices[i]);
  }
}

}  // namespace

namespace logging_filter_detail {

CallData::CallData(bool is_client,
                   const ClientMetadata& client_initial_metadata,
                   const std::string& authority)
    : call_id_(GetCallId()) {
  absl::string_view path;
  if (auto* value = client_initial_metadata.get_pointer(HttpPathMetadata())) {
    path = value->as_string_view();
  }
  std::vector<std::string> parts = absl::StrSplit(path, '/', absl::SkipEmpty());
  if (parts.size() == 2) {
    service_name_ = std::move(parts[0]);
    method_name_ = std::move(parts[1]);
  }
  config_ = g_logging_sink->FindMatch(is_client, service_name_, method_name_);
  if (config_.ShouldLog()) {
    if (auto* value =
            client_initial_metadata.get_pointer(HttpAuthorityMetadata())) {
      authority_ = std::string(value->as_string_view());
    } else {
      authority_ = authority;
    }
  }
}

void CallData::LogClientHeader(bool is_client,
                               CallTracerAnnotationInterface* tracer,
                               const ClientMetadata& metadata) {
  LoggingSink::Entry entry;
  if (!is_client) {
    if (auto* value = metadata.get_pointer(PeerString())) {
      peer_ = PeerStringToAddress(*value);
    }
  }
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kClientHeader);
  MetadataEncoder encoder(&entry.payload, nullptr,
                          config_.max_metadata_bytes());
  metadata.Encode(&encoder);
  entry.payload_truncated = encoder.truncated();
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::LogClientHalfClose(bool is_client,
                                  CallTracerAnnotationInterface* tracer) {
  LoggingSink::Entry entry;
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kClientHalfClose);
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::LogServerHeader(bool is_client,
                               CallTracerAnnotationInterface* tracer,
                               const ServerMetadata* metadata) {
  LoggingSink::Entry entry;
  if (metadata != nullptr) {
    entry.is_trailer_only = metadata->get(GrpcTrailersOnly()).value_or(false);
    if (is_client) {
      if (auto* value = metadata->get_pointer(PeerString())) {
        peer_ = PeerStringToAddress(*value);
      }
    }
  }
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kServerHeader);
  if (metadata != nullptr) {
    MetadataEncoder encoder(&entry.payload, nullptr,
                            config_.max_metadata_bytes());
    metadata->Encode(&encoder);
    entry.payload_truncated = encoder.truncated();
  }
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::LogServerTrailer(bool is_client,
                                CallTracerAnnotationInterface* tracer,
                                const ServerMetadata* metadata) {
  LoggingSink::Entry entry;
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kServerTrailer);
  if (metadata != nullptr) {
    entry.is_trailer_only = metadata->get(GrpcTrailersOnly()).value_or(false);
    MetadataEncoder encoder(&entry.payload, &entry.payload.status_details,
                            config_.max_metadata_bytes());
    metadata->Encode(&encoder);
    entry.payload_truncated = encoder.truncated();
  }
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::LogClientMessage(bool is_client,
                                CallTracerAnnotationInterface* tracer,
                                const SliceBuffer* message) {
  LoggingSink::Entry entry;
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kClientMessage);
  EncodeMessageToPayload(message, config_.max_message_bytes(), &entry);
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::LogServerMessage(bool is_client,
                                CallTracerAnnotationInterface* tracer,
                                const SliceBuffer* message) {
  LoggingSink::Entry entry;
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kServerMessage);
  EncodeMessageToPayload(message, config_.max_message_bytes(), &entry);
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::LogCancel(bool is_client,
                         CallTracerAnnotationInterface* tracer) {
  LoggingSink::Entry entry;
  SetCommonEntryFields(&entry, is_client, tracer,
                       LoggingSink::Entry::EventType::kCancel);
  g_logging_sink->LogEntry(std::move(entry));
}

void CallData::SetCommonEntryFields(LoggingSink::Entry* entry, bool is_client,
                                    CallTracerAnnotationInterface* tracer,
                                    LoggingSink::Entry::EventType event_type) {
  entry->call_id = call_id_;
  entry->sequence_id = sequence_id_++;
  entry->type = event_type;
  entry->logger = is_client ? LoggingSink::Entry::Logger::kClient
                            : LoggingSink::Entry::Logger::kServer;
  entry->authority = authority_;
  entry->peer = peer_;
  entry->service_name = service_name_;
  entry->method_name = method_name_;
  entry->timestamp = Timestamp::Now();
  if (tracer != nullptr) {
    entry->trace_id = tracer->TraceId();
    entry->span_id = tracer->SpanId();
    entry->is_sampled = tracer->IsSampled();
  }
}

}  // namespace logging_filter_detail

absl::StatusOr<std::unique_ptr<ClientLoggingFilter>>
ClientLoggingFilter::Create(const ChannelArgs& args,
                            ChannelFilter::Args /*filter_args*/) {
  std::optional<absl::string_view> default_authority =
      args.GetString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (default_authority.has_value()) {
    return std::make_unique<ClientLoggingFilter>(
        std::string(default_authority.value()));
  }
  std::optional<std::string> server_uri =
      args.GetOwnedString(GRPC_ARG_SERVER_URI);
  if (server_uri.has_value()) {
    return std::make_unique<ClientLoggingFilter>(
        CoreConfiguration::Get().resolver_registry().GetDefaultAuthority(
            *server_uri));
  }
  return std::make_unique<ClientLoggingFilter>("");
}

void ClientLoggingFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ClientLoggingFilter* filter) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ClientLoggingFilter::Call::OnClientInitialMetadata");
  call_data_.emplace(true, md, filter->default_authority_);
  if (!call_data_->ShouldLog()) {
    call_data_.reset();
    return;
  }
  call_data_->LogClientHeader(
      /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>(), md);
}

void ClientLoggingFilter::Call::OnServerInitialMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ClientLoggingFilter::Call::OnServerInitialMetadata");
  if (!call_data_.has_value()) return;
  call_data_->LogServerHeader(
      /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>(),
      &md);
}

void ClientLoggingFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ClientLoggingFilter::Call::OnServerTrailingMetadata");
  if (!call_data_.has_value()) return;
  if (md.get(GrpcCallWasCancelled()).value_or(false) &&
      md.get(GrpcStatusMetadata()) == GRPC_STATUS_CANCELLED) {
    call_data_->LogCancel(
        /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>());
    return;
  }
  call_data_->LogServerTrailer(
      /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>(),
      &md);
}

void ClientLoggingFilter::Call::OnClientToServerMessage(
    const Message& message) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ClientLoggingFilter::Call::OnClientToServerMessage");
  if (!call_data_.has_value()) return;
  call_data_->LogClientMessage(
      /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>(),
      message.payload());
}

void ClientLoggingFilter::Call::OnClientToServerHalfClose() {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ClientLoggingFilter::Call::OnClientToServerHalfClose");
  if (!call_data_.has_value()) return;
  call_data_->LogClientHalfClose(
      /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>());
}

void ClientLoggingFilter::Call::OnServerToClientMessage(
    const Message& message) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ClientLoggingFilter::Call::OnServerToClientMessage");
  if (!call_data_.has_value()) return;
  call_data_->LogServerMessage(
      /*is_client=*/true, MaybeGetContext<CallTracerAnnotationInterface>(),
      message.payload());
}

const grpc_channel_filter ClientLoggingFilter::kFilter =
    MakePromiseBasedFilter<ClientLoggingFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>();

absl::StatusOr<std::unique_ptr<ServerLoggingFilter>>
ServerLoggingFilter::Create(const ChannelArgs& /*args*/,
                            ChannelFilter::Args /*filter_args*/) {
  return std::make_unique<ServerLoggingFilter>();
}

// Construct a promise for one call.
void ServerLoggingFilter::Call::OnClientInitialMetadata(ClientMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ServerLoggingFilter::Call::OnClientInitialMetadata");
  call_data_.emplace(false, md, "");
  if (!call_data_->ShouldLog()) {
    call_data_.reset();
    return;
  }
  call_data_->LogClientHeader(
      /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>(),
      md);
}

void ServerLoggingFilter::Call::OnServerInitialMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ServerLoggingFilter::Call::OnServerInitialMetadata");
  if (!call_data_.has_value()) return;
  call_data_->LogServerHeader(
      /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>(),
      &md);
}

void ServerLoggingFilter::Call::OnServerTrailingMetadata(ServerMetadata& md) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ServerLoggingFilter::Call::OnServerTrailingMetadata");
  if (!call_data_.has_value()) return;
  if (md.get(GrpcCallWasCancelled()).value_or(false) &&
      md.get(GrpcStatusMetadata()) == GRPC_STATUS_CANCELLED) {
    call_data_->LogCancel(
        /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>());
    return;
  }
  call_data_->LogServerTrailer(
      /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>(),
      &md);
}

void ServerLoggingFilter::Call::OnClientToServerMessage(
    const Message& message) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ServerLoggingFilter::Call::OnClientToServerMessage");
  if (!call_data_.has_value()) return;
  call_data_->LogClientMessage(
      /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>(),
      message.payload());
}

void ServerLoggingFilter::Call::OnClientToServerHalfClose() {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ServerLoggingFilter::Call::OnClientToServerHalfClose");
  if (!call_data_.has_value()) return;
  call_data_->LogClientHalfClose(
      /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>());
}

void ServerLoggingFilter::Call::OnServerToClientMessage(
    const Message& message) {
  GRPC_LATENT_SEE_INNER_SCOPE(
      "ServerLoggingFilter::Call::OnServerToClientMessage");
  if (!call_data_.has_value()) return;
  call_data_->LogServerMessage(
      /*is_client=*/false, MaybeGetContext<CallTracerAnnotationInterface>(),
      message.payload());
}

const grpc_channel_filter ServerLoggingFilter::kFilter =
    MakePromiseBasedFilter<ServerLoggingFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>();

void RegisterLoggingFilter(LoggingSink* sink) {
  g_logging_sink = sink;
  CoreConfiguration::RegisterEphemeralBuilder(
      [](CoreConfiguration::Builder* builder) {
        builder->channel_init()
            ->RegisterV2Filter<ServerLoggingFilter>(GRPC_SERVER_CHANNEL)
            // TODO(yashykt) : Figure out a good place to place this channel arg
            .IfChannelArg("grpc.experimental.enable_observability", true);
        builder->channel_init()
            ->RegisterV2Filter<ClientLoggingFilter>(GRPC_CLIENT_CHANNEL)
            // TODO(yashykt) : Figure out a good place to place this channel arg
            .IfChannelArg("grpc.experimental.enable_observability", true);
      });
}

}  // namespace grpc_core
