//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_FILTER_H
#define GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_FILTER_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

// struct envoy_service_ext_proc_v3_ProcessingRequest;
// struct envoy_config_core_v3_HeaderMap;
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/ext_proc/v3/external_processor.upb.h"
#include "src/core/call/call_destination.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "upb/base/string_view.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

struct PipeOwner {
  InterActivityLatch<ClientMetadataHandle> client_initial_metadata;
  InterActivityPipe<MessageHandle, 1> client_to_server_messages;
  InterActivityLatch<std::optional<ServerMetadataHandle>>
      server_initial_metadata;
  InterActivityPipe<MessageHandle, 1> server_to_client_messages;
  InterActivityLatch<ServerMetadataHandle> server_trailing_metadata;
};

enum class BodySendMode {
  kNone = 0,
  kGrpc,
};

struct ExtProcResponse {
  struct HeaderMutation {
    std::vector<std::pair<std::string, std::string>> set_headers;
    std::vector<std::string> remove_headers;
  };
  std::optional<HeaderMutation> request_headers;
  std::optional<HeaderMutation> response_headers;
  std::optional<HeaderMutation> response_trailers;
  struct BodyMutation {
    std::string body;
    bool end_of_stream = false;
    bool end_of_stream_without_message = false;
  };
  std::optional<BodyMutation> request_body;
  std::optional<BodyMutation> response_body;
  struct ImmediateResponse {
    uint32_t status;
    HeaderMutation header_mutation;
    std::string details;
  };
  std::optional<ImmediateResponse> immediate_response;
  bool mode_override = false;
  bool request_drain = false;
};

absl::StatusOr<ExtProcResponse> ParseExtProcResponse(
    envoy_service_ext_proc_v3_ProcessingResponse* response);

class ExtProcRequest {
 public:
  class Builder {
   public:
    explicit Builder(upb_Arena* arena);
    Builder& SetRequestHeaders(envoy_config_core_v3_HeaderMap* headers,
                               bool end_of_stream);
    Builder& SetResponseHeaders(envoy_config_core_v3_HeaderMap* headers,
                                bool end_of_stream);
    Builder& SetRequestBody(upb_StringView buf, bool end_of_stream);
    Builder& SetResponseBody(upb_StringView buf, bool end_of_stream);
    Builder& SetResponseTrailers(envoy_config_core_v3_HeaderMap* trailer);
    Builder& SetObservabilityMode(bool mode);
    Builder& SetAttributes(
        const std::map<std::string, std::string>& attributes);
    Builder& SetProtocolConfigRequest(bool is_first_message, BodySendMode mode);
    Builder& SetProtocolConfigResponse(bool is_first_message,
                                       BodySendMode mode);

    ExtProcRequest Build();

   private:
    upb_Arena* arena_;
    envoy_service_ext_proc_v3_ProcessingRequest* request_;
  };

  std::string SerializeMessage();

 private:
  explicit ExtProcRequest(upb_Arena* arena,
                          envoy_service_ext_proc_v3_ProcessingRequest* request);

  upb_Arena* arena_;
  envoy_service_ext_proc_v3_ProcessingRequest* request_;
};

class ExtProcFilter final : public V3InterceptorToV2Bridge<ExtProcFilter> {
 public:
  struct ProcessingMode {
    // nullopt is DEFAULT, true is SEND, false is SKIP
    std::optional<bool> send_request_headers;
    std::optional<bool> send_response_headers;
    std::optional<bool> send_response_trailers;

    // true is GRPC, false is NONE
    bool send_request_body;
    bool send_response_body;

    bool operator==(const ProcessingMode& other) const {
      return send_request_headers == other.send_request_headers &&
             send_response_headers == other.send_response_headers &&
             send_response_trailers == other.send_response_trailers &&
             send_request_body == other.send_request_body &&
             send_response_body == other.send_response_body;
    }

    std::string ToString() const;
  };

  // Top-level filter config.
  struct Config final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_proc_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      const auto& o = DownCast<const Config&>(other);
      return grpc_service == o.grpc_service &&
             failure_mode_allow == o.failure_mode_allow &&
             processing_mode == o.processing_mode &&
             allow_mode_override == o.allow_mode_override &&
             allowed_override_modes == o.allowed_override_modes &&
             request_attributes == o.request_attributes &&
             response_attributes == o.response_attributes &&
             mutation_rules == o.mutation_rules &&
             forwarding_allowed_headers == o.forwarding_allowed_headers &&
             forwarding_disallowed_headers == o.forwarding_disallowed_headers &&
             disable_immediate_response == o.disable_immediate_response &&
             observability_mode == o.observability_mode &&
             deferred_close_timeout == o.deferred_close_timeout;
    }

    std::string ToString() const override;

    std::shared_ptr<XdsGrpcService> grpc_service;
    bool failure_mode_allow;
    ProcessingMode processing_mode;
    bool allow_mode_override;
    std::vector<ProcessingMode> allowed_override_modes;
    std::vector<std::string> request_attributes;
    std::vector<std::string> response_attributes;
    std::optional<HeaderMutationRules> mutation_rules;
    std::vector<StringMatcher> forwarding_allowed_headers;
    std::vector<StringMatcher> forwarding_disallowed_headers;
    bool disable_immediate_response;
    bool observability_mode;
    Duration deferred_close_timeout;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "ext_proc"; }

  static absl::StatusOr<RefCountedPtr<ExtProcFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  ExtProcFilter(const ChannelArgs& args, RefCountedPtr<const Config> config,
                ChannelFilter::Args filter_args);

 private:
  void Orphaned() override {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

  auto ClientInitialMetadata(CallHandler handler);
  auto StartCallLoops(CallHandler handler, PipeOwner* pipe_owner,
                      ClientMetadataHandle metadata);
  auto ClientToServerMessages(CallHandler handler, CallInitiator initiator,
                              PipeOwner* pipe_owner);
  auto ServerInitialMetadata(CallHandler handler, CallInitiator initiator,
                             PipeOwner* pipe_owner);
  auto ServerToClientMessages(CallHandler handler, CallInitiator initiator,
                              PipeOwner* pipe_owner);
  auto ServerTrailingMetadata(CallHandler handler, CallInitiator initiator,
                              PipeOwner* pipe_owner);

  RefCountedPtr<const Config> config_;
};

extern void (*g_test_ext_proc_metadata_modifier)(grpc_metadata_batch*);
extern void (*g_test_ext_proc_message_modifier)(MessageHandle*);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_EXT_PROC_EXT_PROC_FILTER_H