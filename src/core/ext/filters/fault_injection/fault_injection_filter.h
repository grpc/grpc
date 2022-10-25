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

#ifndef GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H
#define GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/random/random.h"
#include "absl/status/statusor.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

// This channel filter is intended to be used by the dynamic filters, instead
// of the ordinary channel stack. The fault injection filter fetches fault
// injection policy from the method config of service config returned by the
// resolver, and enforces the fault injection policy.
class FaultInjectionFilter : public ChannelFilter {
 public:
  static const grpc_channel_filter kFilter;

  static absl::StatusOr<FaultInjectionFilter> Create(
      const ChannelArgs& args, ChannelFilter::Args filter_args);

  // Construct a promise for one call.
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) override;

 private:
  explicit FaultInjectionFilter(ChannelFilter::Args filter_args);

  class InjectionDecision;
  InjectionDecision MakeInjectionDecision(
      const ClientMetadataHandle& initial_metadata);

  // The relative index of instances of the same filter.
  size_t index_;
  const size_t service_config_parser_index_;
  std::unique_ptr<Mutex> mu_;
  absl::InsecureBitGen abort_rand_generator_ ABSL_GUARDED_BY(mu_);
  absl::InsecureBitGen delay_rand_generator_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_FAULT_INJECTION_FAULT_INJECTION_FILTER_H
