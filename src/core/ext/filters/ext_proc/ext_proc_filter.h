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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/core/ext/filters/ext_proc/ext_proc_messages.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/unique_type_name.h"
#include "src/core/xds/grpc/blackboard.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/xds_client/xds_transport.h"

namespace grpc_core {

class ExtProcFilter final : public V3InterceptorToV2Bridge<ExtProcFilter> {
 public:
  class ExtProcChannel;

  using ProcessingMode = ExtProcProcessingMode;

  struct Config final : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("ext_proc_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override {
      const auto& o = DownCast<const Config&>(other);
      auto grpc_service_equals = [](const std::shared_ptr<XdsGrpcService>& a,
                                    const std::shared_ptr<XdsGrpcService>& b) {
        if (a == nullptr && b == nullptr) return true;
        if (a == nullptr || b == nullptr) return false;
        return *a == *b;
      };
      return grpc_service_equals(grpc_service, o.grpc_service) &&
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
  void Orphaned() override {}

  void InterceptCall(UnstartedCallHandler unstarted_call_handler) override;

  RefCountedPtr<XdsTransportFactory> transport_factory_;
  RefCountedPtr<const Config> config_;
  RefCountedPtr<ExtProcChannel> channel_;
  Slice default_authority_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_EXT_PROC_EXT_PROC_FILTER_H
