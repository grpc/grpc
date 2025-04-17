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

#include "test/core/test_util/test_lb_policies.h"

#include <grpc/grpc.h>
#include <grpc/support/json.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <variant>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/core/client_channel/lb_metadata.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/load_balancing/delegating_helper.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/oob_backend_metric.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_util.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/time.h"
#include "src/core/util/uri.h"

namespace grpc_core {

namespace {

//
// ForwardingLoadBalancingPolicy
//

// A minimal forwarding class to avoid implementing a standalone test LB.
class ForwardingLoadBalancingPolicy : public LoadBalancingPolicy {
 public:
  ForwardingLoadBalancingPolicy(
      std::unique_ptr<ChannelControlHelper> delegating_helper, Args args,
      absl::string_view delegate_policy_name, intptr_t initial_refcount = 1)
      : LoadBalancingPolicy(std::move(args), initial_refcount) {
    Args delegate_args;
    delegate_args.work_serializer = work_serializer();
    delegate_args.channel_control_helper = std::move(delegating_helper);
    delegate_args.args = channel_args();
    delegate_ =
        CoreConfiguration::Get().lb_policy_registry().CreateLoadBalancingPolicy(
            delegate_policy_name, std::move(delegate_args));
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
  }

  ~ForwardingLoadBalancingPolicy() override = default;

  absl::Status UpdateLocked(UpdateArgs args) override {
    // Use correct config for the delegate load balancing policy
    auto config =
        CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
            Json::FromArray({Json::FromObject(
                {{std::string(delegate_->name()), Json::FromObject({})}})}));
    CHECK_OK(config);
    args.config = *config;
    return delegate_->UpdateLocked(std::move(args));
  }

  void ExitIdleLocked() override { delegate_->ExitIdleLocked(); }

  void ResetBackoffLocked() override { delegate_->ResetBackoffLocked(); }

 private:
  void ShutdownLocked() override { delegate_.reset(); }

  OrphanablePtr<LoadBalancingPolicy> delegate_;
};

//
// TestPickArgsLb
//

constexpr absl::string_view kTestPickArgsLbPolicyName = "test_pick_args_lb";

class TestPickArgsLb : public ForwardingLoadBalancingPolicy {
 public:
  TestPickArgsLb(Args args, TestPickArgsCallback cb,
                 absl::string_view delegate_policy_name)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(RefCountedPtr<TestPickArgsLb>(this), cb),
            std::move(args), delegate_policy_name,
            /*initial_refcount=*/2) {}

  ~TestPickArgsLb() override = default;

  absl::string_view name() const override { return kTestPickArgsLbPolicyName; }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<SubchannelPicker> delegate_picker,
           TestPickArgsCallback cb)
        : delegate_picker_(std::move(delegate_picker)), cb_(std::move(cb)) {}

    PickResult Pick(PickArgs args) override {
      // Report args seen.
      PickArgsSeen args_seen;
      args_seen.path = std::string(args.path);
      args_seen.metadata =
          DownCast<LbMetadata*>(args.initial_metadata)->TestOnlyCopyToVector();
      cb_(args_seen);
      // Do pick.
      return delegate_picker_->Pick(args);
    }

   private:
    RefCountedPtr<SubchannelPicker> delegate_picker_;
    TestPickArgsCallback cb_;
  };

  class Helper
      : public ParentOwningDelegatingChannelControlHelper<TestPickArgsLb> {
   public:
    Helper(RefCountedPtr<TestPickArgsLb> parent, TestPickArgsCallback cb)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)),
          cb_(std::move(cb)) {}

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      parent_helper()->UpdateState(
          state, status, MakeRefCounted<Picker>(std::move(picker), cb_));
    }

   private:
    TestPickArgsCallback cb_;
  };
};

class TestPickArgsLbConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kTestPickArgsLbPolicyName; }
};

class TestPickArgsLbFactory : public LoadBalancingPolicyFactory {
 public:
  explicit TestPickArgsLbFactory(TestPickArgsCallback cb,
                                 absl::string_view delegate_policy_name)
      : cb_(std::move(cb)), delegate_policy_name_(delegate_policy_name) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<TestPickArgsLb>(std::move(args), cb_,
                                          delegate_policy_name_);
  }

  absl::string_view name() const override { return kTestPickArgsLbPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<TestPickArgsLbConfig>();
  }

 private:
  TestPickArgsCallback cb_;
  std::string delegate_policy_name_;
};

//
// InterceptRecvTrailingMetadataLoadBalancingPolicy
//

constexpr char kInterceptRecvTrailingMetadataLbPolicyName[] =
    "intercept_trailing_metadata_lb";

class InterceptRecvTrailingMetadataLoadBalancingPolicy
    : public ForwardingLoadBalancingPolicy {
 public:
  InterceptRecvTrailingMetadataLoadBalancingPolicy(
      Args args, InterceptRecvTrailingMetadataCallback cb)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(
                RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy>(
                    this),
                std::move(cb)),
            std::move(args),
            /*delegate_policy_name=*/"pick_first",
            /*initial_refcount=*/2) {}

  ~InterceptRecvTrailingMetadataLoadBalancingPolicy() override = default;

  absl::string_view name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<SubchannelPicker> delegate_picker,
           InterceptRecvTrailingMetadataCallback cb)
        : delegate_picker_(std::move(delegate_picker)), cb_(std::move(cb)) {}

    PickResult Pick(PickArgs args) override {
      // Do pick.
      PickResult result = delegate_picker_->Pick(args);
      // Intercept trailing metadata.
      auto* complete_pick = std::get_if<PickResult::Complete>(&result.result);
      if (complete_pick != nullptr) {
        complete_pick->subchannel_call_tracker =
            std::make_unique<SubchannelCallTracker>(cb_);
      }
      return result;
    }

   private:
    RefCountedPtr<SubchannelPicker> delegate_picker_;
    InterceptRecvTrailingMetadataCallback cb_;
  };

  class Helper : public ParentOwningDelegatingChannelControlHelper<
                     InterceptRecvTrailingMetadataLoadBalancingPolicy> {
   public:
    Helper(
        RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy> parent,
        InterceptRecvTrailingMetadataCallback cb)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)),
          cb_(std::move(cb)) {}

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      parent_helper()->UpdateState(
          state, status, MakeRefCounted<Picker>(std::move(picker), cb_));
    }

   private:
    InterceptRecvTrailingMetadataCallback cb_;
  };

  class SubchannelCallTracker : public SubchannelCallTrackerInterface {
   public:
    explicit SubchannelCallTracker(InterceptRecvTrailingMetadataCallback cb)
        : cb_(std::move(cb)) {}

    void Start() override {}

    void Finish(FinishArgs args) override {
      TrailingMetadataArgsSeen args_seen;
      args_seen.status = args.status;
      args_seen.backend_metric_data =
          args.backend_metric_accessor->GetBackendMetricData();
      args_seen.metadata =
          DownCast<LbMetadata*>(args.trailing_metadata)->TestOnlyCopyToVector();
      cb_(args_seen);
    }

   private:
    InterceptRecvTrailingMetadataCallback cb_;
  };
};

class InterceptTrailingConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }
};

class InterceptTrailingFactory : public LoadBalancingPolicyFactory {
 public:
  explicit InterceptTrailingFactory(InterceptRecvTrailingMetadataCallback cb)
      : cb_(std::move(cb)) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<InterceptRecvTrailingMetadataLoadBalancingPolicy>(
        std::move(args), cb_);
  }

  absl::string_view name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<InterceptTrailingConfig>();
  }

 private:
  InterceptRecvTrailingMetadataCallback cb_;
};

//
// AddressTestLoadBalancingPolicy
//

constexpr char kAddressTestLbPolicyName[] = "address_test_lb";

class AddressTestLoadBalancingPolicy : public ForwardingLoadBalancingPolicy {
 public:
  AddressTestLoadBalancingPolicy(Args args, AddressTestCallback cb)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(
                RefCountedPtr<AddressTestLoadBalancingPolicy>(this),
                std::move(cb)),
            std::move(args),
            /*delegate_policy_name=*/"pick_first",
            /*initial_refcount=*/2) {}

  ~AddressTestLoadBalancingPolicy() override = default;

  absl::string_view name() const override { return kAddressTestLbPolicyName; }

 private:
  class Helper : public ParentOwningDelegatingChannelControlHelper<
                     AddressTestLoadBalancingPolicy> {
   public:
    Helper(RefCountedPtr<AddressTestLoadBalancingPolicy> parent,
           AddressTestCallback cb)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)),
          cb_(std::move(cb)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args) override {
      cb_(EndpointAddresses(address, per_address_args));
      return parent_helper()->CreateSubchannel(address, per_address_args, args);
    }

   private:
    AddressTestCallback cb_;
  };
};

class AddressTestConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kAddressTestLbPolicyName; }
};

class AddressTestFactory : public LoadBalancingPolicyFactory {
 public:
  explicit AddressTestFactory(AddressTestCallback cb) : cb_(std::move(cb)) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<AddressTestLoadBalancingPolicy>(std::move(args), cb_);
  }

  absl::string_view name() const override { return kAddressTestLbPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<AddressTestConfig>();
  }

 private:
  AddressTestCallback cb_;
};

//
// FixedAddressLoadBalancingPolicy
//

constexpr char kFixedAddressLbPolicyName[] = "fixed_address_lb";

class FixedAddressConfig : public LoadBalancingPolicy::Config {
 public:
  explicit FixedAddressConfig(std::string address)
      : address_(std::move(address)) {}

  absl::string_view name() const override { return kFixedAddressLbPolicyName; }

  const std::string& address() const { return address_; }

 private:
  std::string address_;
};

class FixedAddressLoadBalancingPolicy : public ForwardingLoadBalancingPolicy {
 public:
  explicit FixedAddressLoadBalancingPolicy(Args args)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(
                RefCountedPtr<FixedAddressLoadBalancingPolicy>(this)),
            std::move(args),
            /*delegate_policy_name=*/"pick_first",
            /*initial_refcount=*/2) {}

  ~FixedAddressLoadBalancingPolicy() override = default;

  absl::string_view name() const override { return kFixedAddressLbPolicyName; }

  absl::Status UpdateLocked(UpdateArgs args) override {
    auto* config = static_cast<FixedAddressConfig*>(args.config.get());
    LOG(INFO) << kFixedAddressLbPolicyName
              << ": update URI: " << config->address();
    auto uri = URI::Parse(config->address());
    args.config.reset();
    EndpointAddressesList addresses;
    if (uri.ok()) {
      grpc_resolved_address address;
      CHECK(grpc_parse_uri(*uri, &address));
      addresses.emplace_back(address, ChannelArgs());
    } else {
      LOG(ERROR) << kFixedAddressLbPolicyName << ": could not parse URI ("
                 << uri.status().ToString() << "), using empty address list";
      args.resolution_note = "no address in fixed_address_lb policy";
    }
    args.addresses =
        std::make_shared<EndpointAddressesListIterator>(std::move(addresses));
    return ForwardingLoadBalancingPolicy::UpdateLocked(std::move(args));
  }

 private:
  using Helper = ParentOwningDelegatingChannelControlHelper<
      FixedAddressLoadBalancingPolicy>;
};

class FixedAddressFactory : public LoadBalancingPolicyFactory {
 public:
  FixedAddressFactory() = default;

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<FixedAddressLoadBalancingPolicy>(std::move(args));
  }

  absl::string_view name() const override { return kFixedAddressLbPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    std::vector<grpc_error_handle> error_list;
    std::string address;
    ParseJsonObjectField(json.object(), "address", &address, &error_list);
    if (!error_list.empty()) {
      grpc_error_handle error = GRPC_ERROR_CREATE_FROM_VECTOR(
          "errors parsing fixed_address_lb config", &error_list);
      absl::Status status = absl::InvalidArgumentError(StatusToString(error));
      return status;
    }
    return MakeRefCounted<FixedAddressConfig>(std::move(address));
  }
};

//
// OobBackendMetricTestLoadBalancingPolicy
//

constexpr char kOobBackendMetricTestLbPolicyName[] =
    "oob_backend_metric_test_lb";

class OobBackendMetricTestConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override {
    return kOobBackendMetricTestLbPolicyName;
  }
};

class OobBackendMetricTestLoadBalancingPolicy
    : public ForwardingLoadBalancingPolicy {
 public:
  OobBackendMetricTestLoadBalancingPolicy(Args args,
                                          OobBackendMetricCallback cb)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(
                RefCountedPtr<OobBackendMetricTestLoadBalancingPolicy>(this)),
            std::move(args),
            /*delegate_policy_name=*/"pick_first",
            /*initial_refcount=*/2),
        cb_(std::move(cb)) {}

  ~OobBackendMetricTestLoadBalancingPolicy() override = default;

  absl::string_view name() const override {
    return kOobBackendMetricTestLbPolicyName;
  }

 private:
  class BackendMetricWatcher : public OobBackendMetricWatcher {
   public:
    BackendMetricWatcher(
        EndpointAddresses address,
        RefCountedPtr<OobBackendMetricTestLoadBalancingPolicy> parent)
        : address_(std::move(address)), parent_(std::move(parent)) {}

    void OnBackendMetricReport(
        const BackendMetricData& backend_metric_data) override {
      parent_->cb_(address_, backend_metric_data);
    }

   private:
    EndpointAddresses address_;
    RefCountedPtr<OobBackendMetricTestLoadBalancingPolicy> parent_;
  };

  class Helper : public ParentOwningDelegatingChannelControlHelper<
                     OobBackendMetricTestLoadBalancingPolicy> {
   public:
    explicit Helper(
        RefCountedPtr<OobBackendMetricTestLoadBalancingPolicy> parent)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args) override {
      auto subchannel =
          parent_helper()->CreateSubchannel(address, per_address_args, args);
      subchannel->AddDataWatcher(MakeOobBackendMetricWatcher(
          Duration::Seconds(1),
          std::make_unique<BackendMetricWatcher>(
              EndpointAddresses(address, per_address_args),
              parent()
                  ->RefAsSubclass<OobBackendMetricTestLoadBalancingPolicy>())));
      return subchannel;
    }
  };

  OobBackendMetricCallback cb_;
};

class OobBackendMetricTestFactory : public LoadBalancingPolicyFactory {
 public:
  explicit OobBackendMetricTestFactory(OobBackendMetricCallback cb)
      : cb_(std::move(cb)) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<OobBackendMetricTestLoadBalancingPolicy>(
        std::move(args), cb_);
  }

  absl::string_view name() const override {
    return kOobBackendMetricTestLbPolicyName;
  }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<OobBackendMetricTestConfig>();
  }

 private:
  OobBackendMetricCallback cb_;
};

//
// FailLoadBalancingPolicy
//

constexpr char kFailPolicyName[] = "fail_lb";

class FailPolicy : public LoadBalancingPolicy {
 public:
  FailPolicy(Args args, absl::Status status, std::atomic<int>* pick_counter)
      : LoadBalancingPolicy(std::move(args)),
        status_(std::move(status)),
        pick_counter_(pick_counter) {}

  absl::string_view name() const override { return kFailPolicyName; }

  absl::Status UpdateLocked(UpdateArgs) override {
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status_,
        MakeRefCounted<FailPicker>(status_, pick_counter_));
    return absl::OkStatus();
  }

  void ResetBackoffLocked() override {}
  void ShutdownLocked() override {}

 private:
  class FailPicker : public SubchannelPicker {
   public:
    FailPicker(absl::Status status, std::atomic<int>* pick_counter)
        : status_(std::move(status)), pick_counter_(pick_counter) {}

    PickResult Pick(PickArgs /*args*/) override {
      if (pick_counter_ != nullptr) pick_counter_->fetch_add(1);
      return PickResult::Fail(status_);
    }

   private:
    absl::Status status_;
    std::atomic<int>* pick_counter_;
  };

  absl::Status status_;
  std::atomic<int>* pick_counter_;
};

class FailLbConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kFailPolicyName; }
};

class FailLbFactory : public LoadBalancingPolicyFactory {
 public:
  FailLbFactory(absl::Status status, std::atomic<int>* pick_counter)
      : status_(std::move(status)), pick_counter_(pick_counter) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<FailPolicy>(std::move(args), status_, pick_counter_);
  }

  absl::string_view name() const override { return kFailPolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<FailLbConfig>();
  }

 private:
  absl::Status status_;
  std::atomic<int>* pick_counter_;
};

//
// QueueOnceLoadBalancingPolicy - a load balancing policy that provides a Queue
// PickResult at least once, after which it delegates to PickFirst.
//

constexpr char kQueueOncePolicyName[] = "queue_once";

class QueueOnceLoadBalancingPolicy : public ForwardingLoadBalancingPolicy {
 public:
  explicit QueueOnceLoadBalancingPolicy(Args args)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(
                RefCountedPtr<QueueOnceLoadBalancingPolicy>(this)),
            std::move(args), "pick_first",
            /*initial_refcount=*/2) {}

  // We use the standard QueuePicker which invokes ExitIdleLocked() on the first
  // pick.
  void ExitIdleLocked() override {
    bool needs_update = !std::exchange(seen_pick_queued_, true);
    if (needs_update) {
      channel_control_helper()->UpdateState(state_to_update_.state,
                                            state_to_update_.status,
                                            std::move(state_to_update_.picker));
    }
  }

  absl::string_view name() const override { return kQueueOncePolicyName; }

 private:
  class Helper : public ParentOwningDelegatingChannelControlHelper<
                     QueueOnceLoadBalancingPolicy> {
   public:
    explicit Helper(RefCountedPtr<QueueOnceLoadBalancingPolicy> parent)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      // If we've already seen a queued pick, just propagate the update
      // directly.
      if (parent()->seen_pick_queued_) {
        parent()->channel_control_helper()->UpdateState(state, status,
                                                        std::move(picker));
        return;
      }
      // Otherwise, store the update in the LB policy, to be propagated later,
      // and return a queueing picker.
      parent()->state_to_update_ = {state, status, std::move(picker)};
      parent_helper()->UpdateState(
          state, status, MakeRefCounted<QueuePicker>(parent()->Ref()));
    }
  };
  struct StateToUpdate {
    grpc_connectivity_state state;
    absl::Status status;
    RefCountedPtr<SubchannelPicker> picker;
  };
  StateToUpdate state_to_update_;
  bool seen_pick_queued_ = false;  // Has a pick been queued yet. Only accessed
                                   // from within the WorkSerializer.
};

class QueueOnceLbConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kQueueOncePolicyName; }
};

class QueueOnceLoadBalancingPolicyFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<QueueOnceLoadBalancingPolicy>(std::move(args));
  }

  absl::string_view name() const override { return kQueueOncePolicyName; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<QueueOnceLbConfig>();
  }
};

//
// AuthorityOverrideLbPolicy: A load balancing policy that delegates to
// pick_first but adds an authority override on completed picks.
//

constexpr char kAuthorityOverridePolicyName[] = "authority_override_lb";

class AuthorityOverrideLoadBalancingPolicy
    : public ForwardingLoadBalancingPolicy {
 public:
  explicit AuthorityOverrideLoadBalancingPolicy(Args args)
      : ForwardingLoadBalancingPolicy(
            std::make_unique<Helper>(
                RefCountedPtr<AuthorityOverrideLoadBalancingPolicy>(this)),
            std::move(args), "pick_first",
            /*initial_refcount=*/2) {}

  absl::string_view name() const override {
    return kAuthorityOverridePolicyName;
  }

  absl::Status UpdateLocked(UpdateArgs args) override {
    authority_override_ =
        grpc_event_engine::experimental::Slice::FromCopiedString(
            args.args.GetString(GRPC_ARG_TEST_LB_AUTHORITY_OVERRIDE)
                .value_or(""));
    return ForwardingLoadBalancingPolicy::UpdateLocked(std::move(args));
  }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(RefCountedPtr<SubchannelPicker> picker,
           grpc_event_engine::experimental::Slice authority_override)
        : picker_(std::move(picker)),
          authority_override_(std::move(authority_override)) {}

    PickResult Pick(PickArgs args) override {
      auto pick_result = picker_->Pick(args);
      auto* complete_pick =
          std::get_if<PickResult::Complete>(&pick_result.result);
      if (complete_pick != nullptr) {
        complete_pick->authority_override = authority_override_.Ref();
      }
      return pick_result;
    }

   private:
    RefCountedPtr<SubchannelPicker> picker_;
    grpc_event_engine::experimental::Slice authority_override_;
  };

  class Helper : public ParentOwningDelegatingChannelControlHelper<
                     AuthorityOverrideLoadBalancingPolicy> {
   public:
    explicit Helper(RefCountedPtr<AuthorityOverrideLoadBalancingPolicy> parent)
        : ParentOwningDelegatingChannelControlHelper(std::move(parent)) {}

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override {
      parent_helper()->UpdateState(
          state, status,
          MakeRefCounted<Picker>(std::move(picker),
                                 parent()->authority_override_.Ref()));
    }
  };

  grpc_event_engine::experimental::Slice authority_override_;
};

class AuthorityOverrideLbConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override {
    return kAuthorityOverridePolicyName;
  }
};

class AuthorityOverrideLoadBalancingPolicyFactory
    : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<AuthorityOverrideLoadBalancingPolicy>(
        std::move(args));
  }

  absl::string_view name() const override {
    return kAuthorityOverridePolicyName;
  }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<AuthorityOverrideLbConfig>();
  }
};

}  // namespace

void RegisterTestPickArgsLoadBalancingPolicy(
    CoreConfiguration::Builder* builder, TestPickArgsCallback cb,
    absl::string_view delegate_policy_name) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<TestPickArgsLbFactory>(std::move(cb),
                                              delegate_policy_name));
}

void RegisterInterceptRecvTrailingMetadataLoadBalancingPolicy(
    CoreConfiguration::Builder* builder,
    InterceptRecvTrailingMetadataCallback cb) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<InterceptTrailingFactory>(std::move(cb)));
}

void RegisterAddressTestLoadBalancingPolicy(CoreConfiguration::Builder* builder,
                                            AddressTestCallback cb) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<AddressTestFactory>(std::move(cb)));
}

void RegisterFixedAddressLoadBalancingPolicy(
    CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<FixedAddressFactory>());
}

void RegisterOobBackendMetricTestLoadBalancingPolicy(
    CoreConfiguration::Builder* builder, OobBackendMetricCallback cb) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<OobBackendMetricTestFactory>(std::move(cb)));
}

void RegisterFailLoadBalancingPolicy(CoreConfiguration::Builder* builder,
                                     absl::Status status,
                                     std::atomic<int>* pick_counter) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<FailLbFactory>(std::move(status), pick_counter));
}

void RegisterQueueOnceLoadBalancingPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<QueueOnceLoadBalancingPolicyFactory>());
}

void RegisterAuthorityOverrideLoadBalancingPolicy(
    CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<AuthorityOverrideLoadBalancingPolicyFactory>());
}

}  // namespace grpc_core
