//
// Copyright 2018 gRPC authors.
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

#ifndef GRPC_TEST_CORE_UTIL_TEST_LB_POLICIES_H
#define GRPC_TEST_CORE_UTIL_TEST_LB_POLICIES_H

#include <grpc/support/port_platform.h>

#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/filters/client_channel/lb_policy/backend_metric_data.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/resolver/server_address.h"

namespace grpc_core {

using MetadataVector = std::vector<std::pair<std::string, std::string>>;

struct PickArgsSeen {
  std::string path;
  MetadataVector metadata;
};

using TestPickArgsCallback = std::function<void(const PickArgsSeen&)>;

// Registers an LB policy called "test_pick_args_lb" that passes the args passed
// to SubchannelPicker::Pick() to cb.
void RegisterTestPickArgsLoadBalancingPolicy(
    CoreConfiguration::Builder* builder, TestPickArgsCallback cb,
    absl::string_view delegate_policy_name = "pick_first");

struct TrailingMetadataArgsSeen {
  absl::Status status;
  const BackendMetricData* backend_metric_data;
  MetadataVector metadata;
};

using InterceptRecvTrailingMetadataCallback =
    std::function<void(const TrailingMetadataArgsSeen&)>;

// Registers an LB policy called "intercept_trailing_metadata_lb" that
// invokes cb when trailing metadata is received for each call.
void RegisterInterceptRecvTrailingMetadataLoadBalancingPolicy(
    CoreConfiguration::Builder* builder,
    InterceptRecvTrailingMetadataCallback cb);

using AddressTestCallback = std::function<void(const ServerAddress&)>;

// Registers an LB policy called "address_test_lb" that invokes cb for each
// address used to create a subchannel.
void RegisterAddressTestLoadBalancingPolicy(CoreConfiguration::Builder* builder,
                                            AddressTestCallback cb);

// Registers an LB policy called "fixed_address_lb" that provides a
// single subchannel whose address is in its configuration.
void RegisterFixedAddressLoadBalancingPolicy(
    CoreConfiguration::Builder* builder);

using OobBackendMetricCallback =
    std::function<void(ServerAddress, const BackendMetricData&)>;

// Registers an LB policy called "oob_backend_metric_test_lb" that invokes
// cb for each OOB backend metric report on each subchannel.
void RegisterOobBackendMetricTestLoadBalancingPolicy(
    CoreConfiguration::Builder* builder, OobBackendMetricCallback cb);

// Registers an LB policy called "fail_lb" that fails all picks with the
// specified status.  If pick_counter is non-null, it will be
// incremented for each pick.
void RegisterFailLoadBalancingPolicy(CoreConfiguration::Builder* builder,
                                     absl::Status status,
                                     std::atomic<int>* pick_counter = nullptr);

// Registers an LB policy called "queue_once" that queues at least one pick, and
// then delegates to PickFirst.
void RegisterQueueOnceLoadBalancingPolicy(CoreConfiguration::Builder* builder);
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_TEST_LB_POLICIES_H
