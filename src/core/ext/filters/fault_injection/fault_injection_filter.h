//
// Copyright 2021 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H
#define GRPC_SRC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H

#include <stddef.h>

#include <memory>

#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/sync.h"
#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"

namespace grpc_core {

// This channel filter is intended to be used by the dynamic filters, instead
// of the ordinary channel stack. The fault injection filter fetches fault
// injection policy from the method config of service config returned by the
// resolver, and enforces the fault injection policy.
class FaultInjectionFilter
    : public ImplementChannelFilter<FaultInjectionFilter> {
 public:
  // TODO(roth): The config structure here does not map cleanly to the
  // xDS representation, and I suspect that we are not handling all of
  // the edge cases correctly (e.g., abort_code=OK).  When we have time,
  // restructure this.
  struct Config : public FilterConfig {
    static UniqueTypeName Type() {
      return GRPC_UNIQUE_TYPE_NAME_HERE("fault_injection_filter_config");
    }
    UniqueTypeName type() const override { return Type(); }

    bool Equals(const FilterConfig& other) const override;
    std::string ToString() const override;

    grpc_status_code abort_code = GRPC_STATUS_OK;
    std::string abort_message = "Fault injected";
    std::string abort_code_header;
    std::string abort_percentage_header;
    uint32_t abort_percentage_numerator = 0;
    uint32_t abort_percentage_denominator = 100;

    Duration delay;
    std::string delay_header;
    std::string delay_percentage_header;
    uint32_t delay_percentage_numerator = 0;
    uint32_t delay_percentage_denominator = 100;

    // By default, the max allowed active faults are unlimited.
    uint32_t max_faults = std::numeric_limits<uint32_t>::max();
  };

  static const grpc_channel_filter kFilterVtable;

  static absl::string_view TypeName() { return "fault_injection_filter"; }

  static absl::StatusOr<std::unique_ptr<FaultInjectionFilter>> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  explicit FaultInjectionFilter(ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  class Call {
   public:
    ArenaPromise<absl::Status> OnClientInitialMetadata(
        ClientMetadata& md, FaultInjectionFilter* filter);
    static inline const NoInterceptor OnServerInitialMetadata;
    static inline const NoInterceptor OnServerTrailingMetadata;
    static inline const NoInterceptor OnClientToServerMessage;
    static inline const NoInterceptor OnClientToServerHalfClose;
    static inline const NoInterceptor OnServerToClientMessage;
    static inline const NoInterceptor OnFinalize;
    channelz::PropertyList ChannelzProperties() {
      return channelz::PropertyList();
    }
  };

 private:
  class InjectionDecision;

  InjectionDecision MakeInjectionDecision(
      const ClientMetadata& initial_metadata);

  // TODO(roth): Remove this method and these data members as part of
  // removing the xds_channel_filter_chain_per_route experiment.
  template <typename T>
  InjectionDecision MakeInjectionDecision(
      const ClientMetadata& initial_metadata, const T& config);
  size_t index_;  // The relative index of instances of the same filter.
  const size_t service_config_parser_index_;

  const RefCountedPtr<const Config> config_;

  Mutex mu_;
  absl::InsecureBitGen abort_rand_generator_ ABSL_GUARDED_BY(mu_);
  absl::InsecureBitGen delay_rand_generator_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H
