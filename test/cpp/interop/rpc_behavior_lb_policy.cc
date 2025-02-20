//
//
// Copyright 2023 gRPC authors.
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

#include "test/cpp/interop/rpc_behavior_lb_policy.h"

#include <grpc/support/port_platform.h>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/util/json/json_args.h"
#include "src/core/util/json/json_object_loader.h"

namespace grpc {
namespace testing {

namespace {

using grpc_core::CoreConfiguration;
using grpc_core::Json;
using grpc_core::JsonArgs;
using grpc_core::JsonLoaderInterface;
using grpc_core::LoadBalancingPolicy;
using grpc_core::OrphanablePtr;
using grpc_core::RefCountedPtr;

constexpr absl::string_view kRpcBehaviorLbPolicyName =
    "test.RpcBehaviorLoadBalancer";

constexpr absl::string_view kRpcBehaviorMetadataKey = "rpc-behavior";

class RpcBehaviorLbPolicyConfig : public LoadBalancingPolicy::Config {
 public:
  static JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto kJsonLoader =
        grpc_core::JsonObjectLoader<RpcBehaviorLbPolicyConfig>()
            .Field("rpcBehavior", &RpcBehaviorLbPolicyConfig::rpc_behavior_)
            .Finish();
    return kJsonLoader;
  }

  absl::string_view rpc_behavior() const { return rpc_behavior_; }

 private:
  absl::string_view name() const override { return kRpcBehaviorLbPolicyName; }

  std::string rpc_behavior_;
};

class RpcBehaviorLbPolicy : public LoadBalancingPolicy {
 public:
  explicit RpcBehaviorLbPolicy(Args args)
      : LoadBalancingPolicy(std::move(args), /*initial_refcount=*/2) {
    Args delegate_args;
    delegate_args.work_serializer = work_serializer();
    delegate_args.args = channel_args();
    delegate_args.channel_control_helper =
        std::make_unique<Helper>(RefCountedPtr<RpcBehaviorLbPolicy>(this));
    delegate_ =
        CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
            "pick_first", std::move(delegate_args));
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
  }

  ~RpcBehaviorLbPolicy() override = default;

  absl::string_view name() const override { return kRpcBehaviorLbPolicyName; }

  absl::Status UpdateLocked(UpdateArgs args) override {
    auto config = args.config.TakeAsSubclass<RpcBehaviorLbPolicyConfig>();
    rpc_behavior_ = std::string(config->rpc_behavior());
    // Use correct config for the delegate load balancing policy
    auto delegate_config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            grpc_core::Json::FromArray({grpc_core::Json::FromObject(
                {{std::string(delegate_->name()),
                  grpc_core::Json::FromObject({})}})}));
    CHECK_OK(delegate_config);
    args.config = std::move(*delegate_config);
    return delegate_->UpdateLocked(std::move(args));
  }

  void ExitIdleLocked() override { delegate_->ExitIdleLocked(); }

  void ResetBackoffLocked() override { delegate_->ResetBackoffLocked(); }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<SubchannelPicker> delegate_picker,
           absl::string_view rpc_behavior)
        : delegate_picker_(std::move(delegate_picker)),
          rpc_behavior_(rpc_behavior) {}

    PickResult Pick(PickArgs args) override {
      // Do pick.
      auto pick_result = delegate_picker_->Pick(args);
      // Add metadata.
      auto* complete_pick =
          std::get_if<PickResult::Complete>(&pick_result.result);
      if (complete_pick != nullptr) {
        complete_pick->metadata_mutations.Set(kRpcBehaviorMetadataKey,
                                              rpc_behavior_);
      }
      // Return result.
      return pick_result;
    }

   private:
    RefCountedPtr<SubchannelPicker> delegate_picker_;
    std::string rpc_behavior_;
  };

  class Helper
      : public ParentOwningDelegatingChannelControlHelper<RpcBehaviorLbPolicy> {
   public:
    explicit Helper(RefCountedPtr<RpcBehaviorLbPolicy> parent)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      parent_helper()->UpdateState(
          state, status,
          grpc_core::MakeRefCounted<Picker>(std::move(picker),
                                            parent()->rpc_behavior_));
    }
  };

  void ShutdownLocked() override {
    grpc_pollset_set_del_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
    delegate_.reset();
  }

  OrphanablePtr<LoadBalancingPolicy> delegate_;
  std::string rpc_behavior_;
};

class RpcBehaviorLbPolicyFactory
    : public grpc_core::LoadBalancingPolicyFactory {
 private:
  absl::string_view name() const override { return kRpcBehaviorLbPolicyName; }

  grpc_core::OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return grpc_core::MakeOrphanable<RpcBehaviorLbPolicy>(std::move(args));
  }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    return grpc_core::LoadFromJson<RefCountedPtr<RpcBehaviorLbPolicyConfig>>(
        json, JsonArgs(), "errors validating LB policy config");
  }
};
}  // namespace

void RegisterRpcBehaviorLbPolicy(
    grpc_core::CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<RpcBehaviorLbPolicyFactory>());
}

}  // namespace testing
}  // namespace grpc
