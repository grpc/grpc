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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/logging/logging_filter.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/logging/logging_sink.h"
#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/uri/uri_parser.h"

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
      gpr_log(
          GPR_DEBUG,
          "Skipped metadata key because of max metadata logging bytes %" PRIu64
          " (current) vs %" PRIu64 " (max less already accounted metadata)",
          mdentry_len, log_len_);
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
    gpr_log(GPR_DEBUG, "peer_string is in invalid format and cannot be logged");
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

class CallData {
 public:
  CallData(bool is_client, const CallArgs& call_args,
           const std::string& authority)
      : call_id_(GetCallId()) {
    absl::string_view path;
    if (auto* value = call_args.client_initial_metadata->get_pointer(
            HttpPathMetadata())) {
      path = value->as_string_view();
    }
    std::vector<std::string> parts =
        absl::StrSplit(path, '/', absl::SkipEmpty());
    if (parts.size() == 2) {
      service_name_ = std::move(parts[0]);
      method_name_ = std::move(parts[1]);
    }
    config_ = g_logging_sink->FindMatch(is_client, service_name_, method_name_);
    if (config_.ShouldLog()) {
      if (auto* value = call_args.client_initial_metadata->get_pointer(
              HttpAuthorityMetadata())) {
        authority_ = std::string(value->as_string_view());
      } else {
        authority_ = authority;
      }
    }
  }

  bool ShouldLog() { return config_.ShouldLog(); }

  void LogClientHeader(bool is_client, CallTracerAnnotationInterface* tracer,
                       const ClientMetadataHandle& metadata) {
    LoggingSink::Entry entry;
    if (!is_client) {
      if (auto* value = metadata->get_pointer(PeerString())) {
        peer_ = PeerStringToAddress(*value);
      }
    }
    SetCommonEntryFields(&entry, is_client, tracer,
                         LoggingSink::Entry::EventType::kClientHeader);
    MetadataEncoder encoder(&entry.payload, nullptr,
                            config_.max_metadata_bytes());
    metadata->Encode(&encoder);
    entry.payload_truncated = encoder.truncated();
    g_logging_sink->LogEntry(std::move(entry));
  }

  void LogClientHalfClose(bool is_client,
                          CallTracerAnnotationInterface* tracer) {
    LoggingSink::Entry entry;
    SetCommonEntryFields(&entry, is_client, tracer,
                         LoggingSink::Entry::EventType::kClientHalfClose);
    g_logging_sink->LogEntry(std::move(entry));
  }

  void LogServerHeader(bool is_client, CallTracerAnnotationInterface* tracer,
                       const ServerMetadata* metadata) {
    LoggingSink::Entry entry;
    if (metadata != nullptr) {
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

  void LogServerTrailer(bool is_client, CallTracerAnnotationInterface* tracer,
                        const ServerMetadata* metadata) {
    LoggingSink::Entry entry;
    SetCommonEntryFields(&entry, is_client, tracer,
                         LoggingSink::Entry::EventType::kServerTrailer);
    if (metadata != nullptr) {
      MetadataEncoder encoder(&entry.payload, &entry.payload.status_details,
                              config_.max_metadata_bytes());
      metadata->Encode(&encoder);
      entry.payload_truncated = encoder.truncated();
    }
    g_logging_sink->LogEntry(std::move(entry));
  }

  void LogClientMessage(bool is_client, CallTracerAnnotationInterface* tracer,
                        const SliceBuffer* message) {
    LoggingSink::Entry entry;
    SetCommonEntryFields(&entry, is_client, tracer,
                         LoggingSink::Entry::EventType::kClientMessage);
    EncodeMessageToPayload(message, config_.max_message_bytes(), &entry);
    g_logging_sink->LogEntry(std::move(entry));
  }

  void LogServerMessage(bool is_client, CallTracerAnnotationInterface* tracer,
                        const SliceBuffer* message) {
    LoggingSink::Entry entry;
    SetCommonEntryFields(&entry, is_client, tracer,
                         LoggingSink::Entry::EventType::kServerMessage);
    EncodeMessageToPayload(message, config_.max_message_bytes(), &entry);
    g_logging_sink->LogEntry(std::move(entry));
  }

  void LogCancel(bool is_client, CallTracerAnnotationInterface* tracer) {
    LoggingSink::Entry entry;
    SetCommonEntryFields(&entry, is_client, tracer,
                         LoggingSink::Entry::EventType::kCancel);
    g_logging_sink->LogEntry(std::move(entry));
  }

 private:
  void SetCommonEntryFields(LoggingSink::Entry* entry, bool is_client,
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
  absl::uint128 call_id_;
  uint32_t sequence_id_ = 0;
  std::string service_name_;
  std::string method_name_;
  std::string authority_;
  LoggingSink::Entry::Address peer_;
  LoggingSink::Config config_;
};

class ClientLoggingFilter final : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientLoggingFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args /*filter_args*/) {
    absl::optional<absl::string_view> default_authority =
        args.GetString(GRPC_ARG_DEFAULT_AUTHORITY);
    if (default_authority.has_value()) {
      return ClientLoggingFilter(std::string(default_authority.value()));
    }
    absl::optional<std::string> server_uri =
        args.GetOwnedString(GRPC_ARG_SERVER_URI);
    if (server_uri.has_value()) {
      return ClientLoggingFilter(
          CoreConfiguration::Get().resolver_registry().GetDefaultAuthority(
              *server_uri));
    }
    return ClientLoggingFilter("");
  }

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override {
    CallData* calld = GetContext<Arena>()->ManagedNew<CallData>(
        true, call_args, default_authority_);
    if (!calld->ShouldLog()) {
      return next_promise_factory(std::move(call_args));
    }
    calld->LogClientHeader(
        /*is_client=*/true,
        static_cast<CallTracerAnnotationInterface*>(
            GetContext<grpc_call_context_element>()
                [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                    .value),
        call_args.client_initial_metadata);
    call_args.server_initial_metadata->InterceptAndMap(
        [calld](ServerMetadataHandle metadata) {
          calld->LogServerHeader(
              /*is_client=*/true,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value),
              metadata.get());
          return metadata;
        });
    call_args.client_to_server_messages->InterceptAndMapWithHalfClose(
        [calld](MessageHandle message) {
          calld->LogClientMessage(
              /*is_client=*/true,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value),
              message->payload());
          return message;
        },
        [calld] {
          calld->LogClientHalfClose(
              /*is_client=*/true,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value));
        });
    call_args.server_to_client_messages->InterceptAndMap(
        [calld](MessageHandle message) {
          calld->LogServerMessage(
              /*is_client=*/true,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value),
              message->payload());
          return message;
        });
    return OnCancel(
        Map(next_promise_factory(std::move(call_args)),
            [calld](ServerMetadataHandle md) {
              calld->LogServerTrailer(
                  /*is_client=*/true,
                  static_cast<CallTracerAnnotationInterface*>(
                      GetContext<grpc_call_context_element>()
                          [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                              .value),
                  md.get());
              return md;
            }),
        [calld]() {
          calld->LogCancel(
              /*is_client=*/true,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value));
        });
  }

 private:
  explicit ClientLoggingFilter(std::string default_authority)
      : default_authority_(std::move(default_authority)) {}
  std::string default_authority_;
};

const grpc_channel_filter ClientLoggingFilter::kFilter =
    MakePromiseBasedFilter<ClientLoggingFilter, FilterEndpoint::kClient,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>("logging");

class ServerLoggingFilter final : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerLoggingFilter> Create(
      const ChannelArgs& /*args*/, ChannelFilter::Args /*filter_args*/) {
    return ServerLoggingFilter();
  }

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override {
    CallData* calld = GetContext<Arena>()->ManagedNew<CallData>(
        false, call_args, /*default_authority=*/"");
    if (!calld->ShouldLog()) {
      return next_promise_factory(std::move(call_args));
    }
    auto* call_tracer = static_cast<CallTracerAnnotationInterface*>(
        GetContext<grpc_call_context_element>()
            [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                .value);
    calld->LogClientHeader(
        /*is_client=*/false, call_tracer, call_args.client_initial_metadata);
    call_args.server_initial_metadata->InterceptAndMap(
        [calld](ServerMetadataHandle metadata) {
          calld->LogServerHeader(
              /*is_client=*/false,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value),
              metadata.get());
          return metadata;
        });
    call_args.client_to_server_messages->InterceptAndMapWithHalfClose(
        [calld](MessageHandle message) {
          calld->LogClientMessage(
              /*is_client=*/false,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value),
              message->payload());
          return message;
        },
        [calld] {
          calld->LogClientHalfClose(
              /*is_client=*/false,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value));
        });
    call_args.server_to_client_messages->InterceptAndMap(
        [calld](MessageHandle message) {
          calld->LogServerMessage(
              /*is_client=*/false,
              static_cast<CallTracerAnnotationInterface*>(
                  GetContext<grpc_call_context_element>()
                      [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                          .value),
              message->payload());
          return message;
        });
    return OnCancel(
        Map(next_promise_factory(std::move(call_args)),
            [calld](ServerMetadataHandle md) {
              calld->LogServerTrailer(
                  /*is_client=*/false,
                  static_cast<CallTracerAnnotationInterface*>(
                      GetContext<grpc_call_context_element>()
                          [GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                              .value),
                  md.get());
              return md;
            }),
        // TODO(yashykt/ctiller): GetContext<grpc_call_context_element> is not
        // valid for the cancellation function requiring us to capture
        // call_tracer.
        [calld, call_tracer]() {
          calld->LogCancel(
              /*is_client=*/false, call_tracer);
        });
  }
};

const grpc_channel_filter ServerLoggingFilter::kFilter =
    MakePromiseBasedFilter<ServerLoggingFilter, FilterEndpoint::kServer,
                           kFilterExaminesServerInitialMetadata |
                               kFilterExaminesInboundMessages |
                               kFilterExaminesOutboundMessages>("logging");

}  // namespace

void RegisterLoggingFilter(LoggingSink* sink) {
  g_logging_sink = sink;
  CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
    builder->channel_init()->RegisterStage(
        GRPC_SERVER_CHANNEL, INT_MAX, [](ChannelStackBuilder* builder) {
          // TODO(yashykt) : Figure out a good place to place this channel
          // arg
          if (builder->channel_args()
                  .GetInt("grpc.experimental.enable_observability")
                  .value_or(true)) {
            builder->PrependFilter(&ServerLoggingFilter::kFilter);
          }
          return true;
        });
    builder->channel_init()->RegisterStage(
        GRPC_CLIENT_CHANNEL, INT_MAX, [](ChannelStackBuilder* builder) {
          // TODO(yashykt) : Figure out a good place to place this channel
          // arg
          if (builder->channel_args()
                  .GetInt("grpc.experimental.enable_observability")
                  .value_or(true)) {
            builder->PrependFilter(&ClientLoggingFilter::kFilter);
          }
          return true;
        });
  });
}

}  // namespace grpc_core
