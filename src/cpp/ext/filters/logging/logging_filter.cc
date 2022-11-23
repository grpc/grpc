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

#include "src/cpp/ext/filters/logging/logging_filter.h"

#include <cstddef>

#include "absl/strings/str_split.h"
#include "logging_sink.h"

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/promise/map_pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_concurrently.h"
#include "src/cpp/ext/filters/logging/logging_sink.h"

namespace grpc {
namespace internal {

namespace {

LoggingSink* g_logging_sink = nullptr;

class LoggingFilter : public grpc_core::ChannelFilter {
 protected:
  explicit LoggingFilter() {}

 private:
};

class MetadataEncoder {
 public:
  MetadataEncoder(LoggingSink::Entry::Payload* payload,
                  absl::string_view* status_details_bin, uint64_t log_len)
      : payload_(payload),
        status_details_bin_(status_details_bin),
        log_len_(log_len) {}

  void Encode(const grpc_core::Slice& key_slice,
              const grpc_core::Slice& value_slice) {
    auto key = key_slice.as_string_view();
    auto value = value_slice.as_string_view();
    if (status_details_bin_ != nullptr && key == "grpc-status-details-bin") {
      *status_details_bin_ = value;
      return;
    }
    if (absl::ConsumePrefix(&key, "grpc-")) {
      // skip all other grpc- headers
      return;
    }
    uint64_t mdentry_len = key.length() + value.length();
    if (mdentry_len > log_len_) {
      gpr_log(GPR_DEBUG,
              "Skipped metadata key because of max metadata logging bytes");
      truncated_ = true;
      return;
    }

    payload_->metadata.emplace(key, value);
    log_len_ -= mdentry_len;
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType&) {}

  bool truncated() const { return truncated_; }

 private:
  LoggingSink::Entry::Payload* const payload_;
  absl::string_view* const status_details_bin_;
  uint64_t log_len_;
  bool truncated_ = false;
};

void SetIpPort(absl::string_view s, LoggingSink::Entry::Address* peer) {
  absl::string_view host;
  absl::string_view port;
  if (grpc_core::SplitHostPort(absl::string_view(s.data(), s.length()), &host,
                               &port) == 1) {
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

LoggingSink::Entry::Address PeerStringToAddress(absl::string_view peer_string) {
  LoggingSink::Entry::Address address;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(peer_string);
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

struct CallData {
  CallData(bool is_client, const grpc_core::CallArgs& call_args) {
    absl::string_view path;
    if (auto* value = call_args.client_initial_metadata->get_pointer(
            grpc_core::HttpPathMetadata())) {
      path = value->as_string_view();
    }
    if (auto* value = call_args.client_initial_metadata->get_pointer(
            grpc_core::HttpAuthorityMetadata())) {
      authority = std::string(value->as_string_view());
    }

    std::vector<absl::string_view> parts =
        absl::StrSplit(path, '/', absl::SkipEmpty());
    if (parts.size() == 2) {
      service_name = std::string(parts[0]);
      method_name = std::string(parts[1]);
    }
    config = g_logging_sink->FindMatch(is_client, service_name, method_name);
  }
  // TODO(yashykt) : Figure out call id
  uint64_t call_id = 0;
  uint32_t sequence_id = 0;
  std::string service_name;
  std::string method_name;
  std::string authority;
  LoggingSink::Config config;
};

class ClientLoggingFilter final : public LoggingFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ClientLoggingFilter> Create(
      const grpc_core::ChannelArgs& args, ChannelFilter::Args filter_args) {
    return ClientLoggingFilter();
  }

  // Construct a promise for one call.
  grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> MakeCallPromise(
      grpc_core::CallArgs call_args,
      grpc_core::NextPromiseFactory next_promise_factory) override {
    gpr_log(GPR_ERROR, "here");
    CallData* calld = grpc_core::GetContext<grpc_core::Arena>()->New<CallData>(
        true, call_args);
    LogClientHeader(calld, call_args.client_initial_metadata);
    auto* server_initial_metadata = call_args.server_initial_metadata;
    auto incoming_mapper =
        grpc_core::PipeMapper<grpc_core::MessageHandle>::Intercept(
            *call_args.incoming_messages);
    grpc_core::PipeMapper<grpc_core::MessageHandle> outgoing_mapper =
        grpc_core::PipeMapper<grpc_core::MessageHandle>::Intercept(
            *call_args.outgoing_messages);
    return grpc_core::TryConcurrently(
               grpc_core::Seq(
                   next_promise_factory(std::move(call_args)),
                   [/* calld */](
                       grpc_core::ServerMetadataHandle metadata) mutable
                   -> grpc_core::ServerMetadataHandle { return metadata; }))
        .NecessaryPull(grpc_core::Seq(
            server_initial_metadata->Wait(),
            // TODO(ctiller): A void return does not work
            [calld](grpc_core::ServerMetadata** server_initial_metadata) mutable
            -> int {
              LogServerHeader(calld, *server_initial_metadata);
              return 0;
            },
            [/*calld, */ incoming_mapper =
                 std::move(incoming_mapper)](int /* prev */) mutable
            -> grpc_core::ArenaPromise<absl::Status> {
              return grpc_core::ImmediateOkStatus();
              // return incoming_mapper.TakeAndRun(
              //     [](grpc_core::MessageHandle message) {
              //       // LogServerMessage();
              //       return message;
              //     });
            }))
        .NecessaryPush(grpc_core::Seq(
            [/*calld, */ outgoing_mapper =
                 std::move(outgoing_mapper)]() mutable {
              // return grpc_core::ImmediateOkStatus();
              return outgoing_mapper.TakeAndRun(
                  [](grpc_core::MessageHandle message) {
                    // LogClientMessage();
                    return message;
                  });
            },
            [/*calld*/]() mutable -> grpc_core::ArenaPromise<absl::Status> {
              // LogClientHalfClose();
              return grpc_core::ImmediateOkStatus();
            }));
  }

 private:
  using LoggingFilter::LoggingFilter;

  static void LogClientHeader(CallData* calld,
                              const grpc_core::ClientMetadataHandle& metadata) {
    if (!calld->config.MetadataLoggingEnabled()) {
      return;
    }
    LoggingSink::Entry entry;
    entry.call_id = calld->call_id;
    entry.sequence_id = calld->sequence_id++;
    entry.type = LoggingSink::Entry::EventType::kClientHeader;
    entry.logger = LoggingSink::Entry::Logger::kClient;
    MetadataEncoder encoder(&entry.payload, nullptr,
                            calld->config.max_metadata_bytes());
    metadata->Encode(&encoder);
    entry.payload_truncated = encoder.truncated();
    // peer is not yet available
    entry.authority = calld->authority;
    entry.service_name = calld->service_name;
    entry.method_name = calld->method_name;
    g_logging_sink->LogEntry(std::move(entry));
  }

  static void LogServerHeader(CallData* calld,
                              const grpc_core::ServerMetadata* metadata) {
    if (!calld->config.MetadataLoggingEnabled()) {
      return;
    }
    LoggingSink::Entry entry;
    entry.call_id = calld->call_id;
    entry.sequence_id = calld->sequence_id++;
    entry.type = LoggingSink::Entry::EventType::kServerHeader;
    entry.logger = LoggingSink::Entry::Logger::kClient;
    if (auto* value =
            metadata->get_pointer(grpc_core::HttpAuthorityMetadata())) {
      entry.authority = std::string(value->as_string_view());
    }
    MetadataEncoder encoder(&entry.payload, nullptr,
                            calld->config.max_metadata_bytes());
    metadata->Encode(&encoder);
    entry.payload_truncated = encoder.truncated();
    if (auto* value = metadata->get_pointer(grpc_core::PeerString())) {
      entry.peer = PeerStringToAddress(*value);
    }
    entry.authority = calld->authority;
    entry.service_name = calld->service_name;
    entry.method_name = calld->method_name;
    g_logging_sink->LogEntry(std::move(entry));
  }
};

const grpc_channel_filter ClientLoggingFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<
        ClientLoggingFilter, grpc_core::FilterEndpoint::kClient,
        grpc_core::kFilterExaminesServerInitialMetadata |
            grpc_core::kFilterExaminesInboundMessages |
            grpc_core::kFilterExaminesOutboundMessages>("logging");

class ServerLoggingFilter final : public LoggingFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<ServerLoggingFilter> Create(
      const grpc_core::ChannelArgs& args, ChannelFilter::Args filter_args) {
    return ServerLoggingFilter();
  }

  // Construct a promise for one call.
  grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> MakeCallPromise(
      grpc_core::CallArgs call_args,
      grpc_core::NextPromiseFactory next_promise_factory) override {
    gpr_log(GPR_ERROR, "here");

    return next_promise_factory(std::move(call_args));
  }

 private:
  using LoggingFilter::LoggingFilter;
};

const grpc_channel_filter ServerLoggingFilter::kFilter =
    grpc_core::MakePromiseBasedFilter<
        ServerLoggingFilter, grpc_core::FilterEndpoint::kServer,
        grpc_core::kFilterExaminesServerInitialMetadata |
            grpc_core::kFilterExaminesInboundMessages |
            grpc_core::kFilterExaminesOutboundMessages>("logging");

}  // namespace

void RegisterLoggingFilter(LoggingSink* sink) {
  g_logging_sink = sink;
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterStage(
            GRPC_SERVER_CHANNEL, INT_MAX,
            [](grpc_core::ChannelStackBuilder* builder) {
              gpr_log(GPR_ERROR, "here");
              // TODO(yashykt) : fix me
              if (builder->channel_args()
                      .GetInt("grpc.experimental.enable_observability")
                      .value_or(true)) {
                gpr_log(GPR_ERROR, "here");
                builder->PrependFilter(&ServerLoggingFilter::kFilter);
              }
              return true;
            });
        builder->channel_init()->RegisterStage(
            GRPC_CLIENT_CHANNEL, INT_MAX,
            [](grpc_core::ChannelStackBuilder* builder) {
              gpr_log(GPR_ERROR, "here");
              // TODO(yashykt) : fix me
              if (builder->channel_args()
                      .GetInt("grpc.experimental.enable_observability")
                      .value_or(true)) {
                gpr_log(GPR_ERROR, "here");
                builder->PrependFilter(&ClientLoggingFilter::kFilter);
              }
              return true;
            });
      });
}

}  // namespace internal
}  // namespace grpc
