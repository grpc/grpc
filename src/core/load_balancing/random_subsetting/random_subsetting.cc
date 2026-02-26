//
// Copyright 2025 gRPC authors.
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

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/validation_errors.h"
#include "src/core/util/work_serializer.h"
#include "src/core/util/xxhash_inline.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

constexpr absl::string_view kRandomSubsetting = "random_subsetting";

class RandomSubsettingConfig final : public LoadBalancingPolicy::Config {
 public:
  RandomSubsettingConfig(
      uint32_t subset_size,
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : subset_size_(subset_size), child_policy_(std::move(child_policy)) {}

  absl::string_view name() const override { return kRandomSubsetting; }

  uint32_t subset_size() const { return subset_size_; }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

 private:
  uint32_t subset_size_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

class RandomSubsetting final : public LoadBalancingPolicy {
 public:
  absl::string_view name() const override { return kRandomSubsetting; }

  explicit RandomSubsetting(Args args);

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;
  void ShutdownLocked() override;
  void ExitIdleLocked() override;

  ~RandomSubsetting() override;

 private:
  EndpointAddressesList FilterEndpoints(const EndpointAddressesList& endpoints,
                                        uint32_t subset_size, uint64_t seed);

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  bool shutting_down_ = false;
  uint64_t seed_;
  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  class Helper final
      : public ParentOwningDelegatingChannelControlHelper<RandomSubsetting> {
   public:
    explicit Helper(RefCountedPtr<RandomSubsetting> random_subsetting_policy)
        : ParentOwningDelegatingChannelControlHelper(
              std::move(random_subsetting_policy)) {}

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      // Simply delegate to parent - no special picker wrapping needed
      parent_helper()->UpdateState(state, status, std::move(picker));
    }
  };
};

RandomSubsetting::RandomSubsetting(Args args)
    : LoadBalancingPolicy(std::move(args)), seed_(SharedBitGen()()) {
  GRPC_TRACE_LOG(random_subsetting_lb, INFO)
      << "[random_subsetting " << this << "] created";
}

absl::Status RandomSubsetting::UpdateLocked(UpdateArgs args) {
  if (shutting_down_) return absl::OkStatus();

  GRPC_TRACE_LOG(random_subsetting_lb, INFO)
      << "[random_subsetting_lb " << this << "] received update";

  auto config = args.config.TakeAsSubclass<RandomSubsettingConfig>();

  // Handle address errors
  if (!args.addresses.ok()) {
    absl::Status status = args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return absl::OkStatus();
  }

  // Convert addresses to list for filtering
  EndpointAddressesList endpoint_list;
  (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
    endpoint_list.push_back(endpoint);
  });

  // Filter endpoints using rendezvous hashing
  auto filtered_endpoints =
      FilterEndpoints(endpoint_list, config->subset_size(), seed_);

  // Create child policy if needed
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }

  // Prepare child update args
  UpdateArgs child_args;
  child_args.addresses = std::make_shared<EndpointAddressesListIterator>(
      std::move(filtered_endpoints));
  child_args.config = config->child_policy();
  child_args.resolution_note = std::move(args.resolution_note);
  child_args.args = std::move(args.args);

  // Update child policy
  GRPC_TRACE_LOG(random_subsetting_lb, INFO)
      << "[random_subsetting_lb " << this << "] updating child policy "
      << child_policy_.get();

  return child_policy_->UpdateLocked(std::move(child_args));
}

void RandomSubsetting::ResetBackoffLocked() {
  if (child_policy_ != nullptr) {
    child_policy_->ResetBackoffLocked();
  }
}

void RandomSubsetting::ShutdownLocked() {
  GRPC_TRACE_LOG(random_subsetting_lb, INFO)
      << "[random_subsetting " << this << "] shutting down";
  shutting_down_ = true;
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
}

void RandomSubsetting::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

RandomSubsetting::~RandomSubsetting() {
  GRPC_TRACE_LOG(random_subsetting_lb, INFO)
      << "[random_subsetting " << this
      << "] destroying random_subsetting LB policy";
}

EndpointAddressesList RandomSubsetting::FilterEndpoints(
    const EndpointAddressesList& endpoints, uint32_t subset_size,
    uint64_t seed) {
  if (subset_size >= endpoints.size()) {
    return endpoints;
  }
  // Create vector of endpoints with their hashes
  struct EndpointWithHash {
    uint64_t hash;
    EndpointAddresses endpoint;
  };
  std::vector<EndpointWithHash> endpoints_with_hash;
  endpoints_with_hash.reserve(endpoints.size());

  // Hash each endpoint using XXH64
  for (const auto& endpoint : endpoints) {
    if (endpoint.addresses().empty()) continue;
    // Use the first address as input to hash function
    const auto& first_address = endpoint.addresses().front();
    std::string address_str =
        grpc_sockaddr_to_string(&first_address, false).value();
    uint64_t hash = XXH64(address_str.data(), address_str.size(), seed);
    endpoints_with_hash.push_back({hash, endpoint});
  }
  // Sort by hash value
  std::sort(endpoints_with_hash.begin(), endpoints_with_hash.end(),
            [](const EndpointWithHash& a, const EndpointWithHash& b) {
              return a.hash < b.hash;
            });
  // Select first subset_size elements
  EndpointAddressesList filtered_endpoints;
  filtered_endpoints.reserve(subset_size);
  for (size_t i = 0; i < subset_size; ++i) {
    filtered_endpoints.push_back(std::move(endpoints_with_hash[i].endpoint));
  }
  return filtered_endpoints;
}

OrphanablePtr<LoadBalancingPolicy> RandomSubsetting::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper = std::make_unique<Helper>(
      RefAsSubclass<RandomSubsetting>(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &random_subsetting_lb_trace);
  GRPC_TRACE_LOG(random_subsetting_lb, INFO)
      << "[random_subsetting_lb " << this
      << "] Created new child policy handler " << lb_policy.get();
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

//
// factory
//

class RandomSubsettingFactory final : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<RandomSubsetting>(std::move(args));
  }

  absl::string_view name() const override { return kRandomSubsetting; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override;
};

absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
RandomSubsettingFactory::ParseLoadBalancingConfig(const Json& json) const {
  ValidationErrors errors;
  uint32_t subset_size = 0;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy;

  // Parse subset_size
  {
    ValidationErrors::ScopedField field(&errors, ".subset_size");
    auto it = json.object().find("subset_size");
    if (it == json.object().end()) {
      errors.AddError("field not present");
    } else if (it->second.type() != Json::Type::kNumber) {
      errors.AddError("must be a number");
    } else {
      if (!absl::SimpleAtoi(it->second.string(), &subset_size)) {
        errors.AddError("failed to parse number");
      } else if (subset_size == 0) {
        errors.AddError("must be greater than 0");
      }
    }
  }

  // Parse childPolicy
  {
    ValidationErrors::ScopedField field(&errors, ".childPolicy");
    auto it = json.object().find("childPolicy");
    if (it == json.object().end()) {
      errors.AddError("field not present");
    } else if (it->second.type() != Json::Type::kArray) {
      errors.AddError("is not an array");
    } else {
      auto child_policy_config = CoreConfiguration::Get()
                                     .lb_policy_registry()
                                     .ParseLoadBalancingConfig(it->second);
      if (!child_policy_config.ok()) {
        errors.AddError(child_policy_config.status().message());
      } else {
        child_policy = std::move(*child_policy_config);
      }
    }
  }

  if (!errors.ok()) {
    return errors.status(
        absl::StatusCode::kInvalidArgument,
        "errors validating random_subsetting LB policy config");
  }

  if (child_policy == nullptr) {  // "should never happen" check
    return absl::InternalError(
        "child policy config is null after successful parsing");
  }

  return MakeRefCounted<RandomSubsettingConfig>(subset_size,
                                                std::move(child_policy));
}

}  // namespace

void RegisterRandomSubsettingLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<RandomSubsettingFactory>());
}

}  // namespace grpc_core
