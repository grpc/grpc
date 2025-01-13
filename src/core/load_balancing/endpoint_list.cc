//
// Copyright 2015 gRPC authors.
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

#include "src/core/load_balancing/endpoint_list.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/json.h>
#include <grpc/support/port_platform.h>
#include <stdlib.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/pick_first/pick_first.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {

//
// EndpointList::Endpoint::Helper
//

class EndpointList::Endpoint::Helper final
    : public LoadBalancingPolicy::DelegatingChannelControlHelper {
 public:
  explicit Helper(RefCountedPtr<Endpoint> endpoint)
      : endpoint_(std::move(endpoint)) {}

  ~Helper() override { endpoint_.reset(DEBUG_LOCATION, "Helper"); }

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& per_address_args,
      const ChannelArgs& args) override {
    return endpoint_->CreateSubchannel(address, per_address_args, args);
  }

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override {
    auto old_state = std::exchange(endpoint_->connectivity_state_, state);
    if (!old_state.has_value()) {
      ++endpoint_->endpoint_list_->num_endpoints_seen_initial_state_;
    }
    endpoint_->picker_ = std::move(picker);
    endpoint_->OnStateUpdate(old_state, state, status);
  }

 private:
  LoadBalancingPolicy::ChannelControlHelper* parent_helper() const override {
    return endpoint_->endpoint_list_->channel_control_helper();
  }

  RefCountedPtr<Endpoint> endpoint_;
};

//
// EndpointList::Endpoint
//

absl::Status EndpointList::Endpoint::Init(
    const EndpointAddresses& addresses, const ChannelArgs& args,
    std::shared_ptr<WorkSerializer> work_serializer) {
  ChannelArgs child_args =
      args.Set(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING, true)
          .Set(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX, true);
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = std::move(work_serializer);
  lb_policy_args.args = child_args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  child_policy_ =
      CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
          "pick_first", std::move(lb_policy_args));
  if (GPR_UNLIKELY(endpoint_list_->tracer_ != nullptr)) {
    LOG(INFO) << "[" << endpoint_list_->tracer_ << " "
              << endpoint_list_->policy_.get() << "] endpoint " << this
              << ": created child policy " << child_policy_.get();
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(
      child_policy_->interested_parties(),
      endpoint_list_->policy_->interested_parties());
  // Construct pick_first config.
  auto config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          Json::FromArray(
              {Json::FromObject({{"pick_first", Json::FromObject({})}})}));
  CHECK(config.ok());
  // Update child policy.
  LoadBalancingPolicy::UpdateArgs update_args;
  update_args.addresses = std::make_shared<SingleEndpointIterator>(addresses);
  update_args.args = child_args;
  update_args.config = std::move(*config);
  return child_policy_->UpdateLocked(std::move(update_args));
}

void EndpointList::Endpoint::Orphan() {
  // Remove pollset_set linkage.
  grpc_pollset_set_del_pollset_set(
      child_policy_->interested_parties(),
      endpoint_list_->policy_->interested_parties());
  child_policy_.reset();
  picker_.reset();
  Unref();
}

void EndpointList::Endpoint::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

void EndpointList::Endpoint::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

size_t EndpointList::Endpoint::Index() const {
  for (size_t i = 0; i < endpoint_list_->endpoints_.size(); ++i) {
    if (endpoint_list_->endpoints_[i].get() == this) return i;
  }
  return -1;
}

RefCountedPtr<SubchannelInterface> EndpointList::Endpoint::CreateSubchannel(
    const grpc_resolved_address& address, const ChannelArgs& per_address_args,
    const ChannelArgs& args) {
  return endpoint_list_->channel_control_helper()->CreateSubchannel(
      address, per_address_args, args);
}

//
// EndpointList
//

void EndpointList::Init(
    EndpointAddressesIterator* endpoints, const ChannelArgs& args,
    absl::FunctionRef<OrphanablePtr<Endpoint>(RefCountedPtr<EndpointList>,
                                              const EndpointAddresses&,
                                              const ChannelArgs&)>
        create_endpoint) {
  if (endpoints == nullptr) return;
  endpoints->ForEach([&](const EndpointAddresses& endpoint) {
    endpoints_.push_back(
        create_endpoint(Ref(DEBUG_LOCATION, "Endpoint"), endpoint, args));
  });
}

void EndpointList::ResetBackoffLocked() {
  for (const auto& endpoint : endpoints_) {
    endpoint->ResetBackoffLocked();
  }
}

void EndpointList::ReportTransientFailure(absl::Status status) {
  if (!resolution_note_.empty()) {
    status = absl::Status(status.code(), absl::StrCat(status.message(), " (",
                                                      resolution_note_, ")"));
  }
  channel_control_helper()->UpdateState(
      GRPC_CHANNEL_TRANSIENT_FAILURE, status,
      MakeRefCounted<LoadBalancingPolicy::TransientFailurePicker>(status));
}

}  // namespace grpc_core
