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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_FILTER_H

#include <queue>

#include "src/core/call/call_destination.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/filters/ext_proc/ext_proc_messages.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/inter_activity_pipe.h"
#include "src/core/lib/promise/map.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/matchers.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class ExtProcFilter final : public V3InterceptorToV2Bridge<ExtProcFilter> {
 public:
  class ExtProcChannel;

  struct ProcessingMode {
    // By default, request and response headers are sent (true), while trailers
    // are skipped (false).
    bool send_request_headers;
    bool send_response_headers;
    bool send_response_trailers;
    // true is GRPC, false is NONE
    bool send_request_body;
    bool send_response_body;

    ProcessingMode()
        : send_request_headers(true),
          send_response_headers(true),
          send_response_trailers(false),
          send_request_body(false),
          send_response_body(false) {}

    ProcessingMode(const ProcessingMode&) = default;
    ProcessingMode& operator=(const ProcessingMode&) = default;
    ProcessingMode(ProcessingMode&&) = default;
    ProcessingMode& operator=(ProcessingMode&&) = default;

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
             request_attributes == o.request_attributes &&
             response_attributes == o.response_attributes &&
             mutation_rules == o.mutation_rules &&
             forwarding_allowed_headers == o.forwarding_allowed_headers &&
             forwarding_disallowed_headers == o.forwarding_disallowed_headers &&
             disable_immediate_response == o.disable_immediate_response &&
             observability_mode == o.observability_mode &&
             deferred_close_timeout == o.deferred_close_timeout &&
             transport_factory == o.transport_factory &&
             instance_name == o.instance_name && channel == o.channel;
    }

    std::string ToString() const override;

    std::shared_ptr<XdsGrpcService> grpc_service;
    bool failure_mode_allow;
    ProcessingMode processing_mode;
    std::vector<std::string> request_attributes;
    std::vector<std::string> response_attributes;
    std::optional<HeaderMutationRules> mutation_rules;
    std::vector<StringMatcher> forwarding_allowed_headers;
    std::vector<StringMatcher> forwarding_disallowed_headers;
    bool disable_immediate_response;
    bool observability_mode;
    Duration deferred_close_timeout;
    RefCountedPtr<XdsTransportFactory> transport_factory;
    std::string instance_name;
    RefCountedPtr<ExtProcChannel> channel;
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "ext_proc"; }

  static absl::StatusOr<RefCountedPtr<ExtProcFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  ExtProcFilter(const ChannelArgs& args, RefCountedPtr<const Config> config,
                ChannelFilter::Args filter_args);

  RefCountedPtr<const Config> config() const { return config_; }
  RefCountedPtr<ExtProcChannel> channel() const { return channel_; }

  class ExtProcChannel final : public Blackboard::Entry {
   public:
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_proc_channel");
    }
    explicit ExtProcChannel(
        std::shared_ptr<const XdsBootstrap::XdsServerTarget> server,
        RefCountedPtr<XdsTransportFactory> transport_factory);
    ~ExtProcChannel() override;
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server() const {
      return server_;
    }

    RefCountedPtr<XdsTransportFactory::XdsTransport> transport() const {
      return transport_;
    }

   private:
    std::shared_ptr<const XdsBootstrap::XdsServerTarget> server_;
    RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;
  };

 private:
  class ExtProcCall;
  void Orphaned() override {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

  auto ProcessServerToClient(CallHandler handler, CallInitiator initiator,
                             RefCountedPtr<ExtProcCall> ext_proc_call);
  auto ServerInitialMetadataNormalMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call,
      std::shared_ptr<ServerMetadataHandle> metadata);
  auto ServerInitialMetadataMaybeObservabilityMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor,
      std::shared_ptr<ServerMetadataHandle> metadata);

  auto SendServerMessageRequest(const MessageHandle& message,
                                ExtProcCall* ext_proc_call,
                                bool send_to_processor);
  auto ServerToClientMessagesMaybeObservabilityMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor);
  auto ServerToClientMessagesNormalModeProducer(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call);
  auto ServerToClientMessagesNormalModeConsumer(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call);
  auto ServerTrailingMetadata(CallHandler handler, CallInitiator initiator,
                              RefCountedPtr<ExtProcCall> ext_proc_call);
  auto ServerTrailingMetadataNormalMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call,
      std::shared_ptr<ServerMetadataHandle> metadata);
  auto ServerTrailingMetadataMaybeObservabilityMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor,
      std::shared_ptr<ServerMetadataHandle> metadata);
  auto ClientToServerMessages(CallHandler handler, CallInitiator initiator,
                              RefCountedPtr<ExtProcCall> ext_proc_call,
                              ::google_protobuf_Struct* attributes);
  auto ClientToServerMessagesMaybeObservabilityMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call, bool send_to_processor,
      ::google_protobuf_Struct* attributes);
  auto ClientToServerMessagesNormalMode(
      CallHandler handler, CallInitiator initiator,
      RefCountedPtr<ExtProcCall> ext_proc_call,
      ::google_protobuf_Struct* attributes);
  auto SendClientMessageRequest(const MessageHandle& message,
                                ExtProcCall* ext_proc_call, bool end_of_stream,
                                bool end_of_stream_without_message,
                                bool send_to_processor,
                                ::google_protobuf_Struct* attributes);

  RefCountedPtr<XdsTransportFactory> transport_factory_;
  RefCountedPtr<const Config> config_;
  RefCountedPtr<ExtProcChannel> channel_;
  Slice default_authority_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_FILTER_H
