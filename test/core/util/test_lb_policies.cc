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

#include "test/core/util/test_lb_policies.h"

#include <string>

#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/transport/connectivity_state.h"

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
      const char* delegate_policy_name, intptr_t initial_refcount = 1)
      : LoadBalancingPolicy(std::move(args), initial_refcount) {
    Args delegate_args;
    delegate_args.work_serializer = work_serializer();
    delegate_args.channel_control_helper = std::move(delegating_helper);
    delegate_args.args = args.args;
    delegate_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
        delegate_policy_name, std::move(delegate_args));
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
  }

  ~ForwardingLoadBalancingPolicy() override = default;

  void UpdateLocked(UpdateArgs args) override {
    delegate_->UpdateLocked(std::move(args));
  }

  void ExitIdleLocked() override { delegate_->ExitIdleLocked(); }

  void ResetBackoffLocked() override { delegate_->ResetBackoffLocked(); }

 private:
  void ShutdownLocked() override { delegate_.reset(); }

  OrphanablePtr<LoadBalancingPolicy> delegate_;
};

//
// CopyMetadataToVector()
//

MetadataVector CopyMetadataToVector(
    LoadBalancingPolicy::MetadataInterface* metadata) {
  MetadataVector result;
  for (const auto& p : *metadata) {
    result.push_back({std::string(p.first), std::string(p.second)});
  }
  return result;
}

//
// TestPickArgsLb
//

constexpr char kTestPickArgsLbPolicyName[] = "test_pick_args_lb";

class TestPickArgsLb : public ForwardingLoadBalancingPolicy {
 public:
  TestPickArgsLb(Args args, TestPickArgsCallback cb,
                 const char* delegate_policy_name)
      : ForwardingLoadBalancingPolicy(
            absl::make_unique<Helper>(RefCountedPtr<TestPickArgsLb>(this), cb),
            std::move(args), delegate_policy_name,
            /*initial_refcount=*/2) {}

  ~TestPickArgsLb() override = default;

  const char* name() const override { return kTestPickArgsLbPolicyName; }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(std::unique_ptr<SubchannelPicker> delegate_picker,
           TestPickArgsCallback cb)
        : delegate_picker_(std::move(delegate_picker)), cb_(std::move(cb)) {}

    PickResult Pick(PickArgs args) override {
      // Report args seen.
      PickArgsSeen args_seen;
      args_seen.path = std::string(args.path);
      args_seen.metadata = CopyMetadataToVector(args.initial_metadata);
      cb_(args_seen);
      // Do pick.
      return delegate_picker_->Pick(args);
    }

   private:
    std::unique_ptr<SubchannelPicker> delegate_picker_;
    TestPickArgsCallback cb_;
  };

  class Helper : public ChannelControlHelper {
   public:
    Helper(RefCountedPtr<TestPickArgsLb> parent, TestPickArgsCallback cb)
        : parent_(std::move(parent)), cb_(std::move(cb)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override {
      return parent_->channel_control_helper()->CreateSubchannel(
          std::move(address), args);
    }

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override {
      parent_->channel_control_helper()->UpdateState(
          state, status, absl::make_unique<Picker>(std::move(picker), cb_));
    }

    void RequestReresolution() override {
      parent_->channel_control_helper()->RequestReresolution();
    }

    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override {
      parent_->channel_control_helper()->AddTraceEvent(severity, message);
    }

   private:
    RefCountedPtr<TestPickArgsLb> parent_;
    TestPickArgsCallback cb_;
  };
};

class TestPickArgsLbConfig : public LoadBalancingPolicy::Config {
 public:
  const char* name() const override { return kTestPickArgsLbPolicyName; }
};

class TestPickArgsLbFactory : public LoadBalancingPolicyFactory {
 public:
  explicit TestPickArgsLbFactory(TestPickArgsCallback cb,
                                 const char* delegate_policy_name)
      : cb_(std::move(cb)), delegate_policy_name_(delegate_policy_name) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<TestPickArgsLb>(std::move(args), cb_,
                                          delegate_policy_name_);
  }

  const char* name() const override { return kTestPickArgsLbPolicyName; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& /*json*/, grpc_error_handle* /*error*/) const override {
    return MakeRefCounted<TestPickArgsLbConfig>();
  }

 private:
  TestPickArgsCallback cb_;
  const char* delegate_policy_name_;
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
            absl::make_unique<Helper>(
                RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy>(
                    this),
                std::move(cb)),
            std::move(args),
            /*delegate_policy_name=*/"pick_first",
            /*initial_refcount=*/2) {}

  ~InterceptRecvTrailingMetadataLoadBalancingPolicy() override = default;

  const char* name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

 private:
  class Picker : public SubchannelPicker {
   public:
    Picker(std::unique_ptr<SubchannelPicker> delegate_picker,
           InterceptRecvTrailingMetadataCallback cb)
        : delegate_picker_(std::move(delegate_picker)), cb_(std::move(cb)) {}

    PickResult Pick(PickArgs args) override {
      // Do pick.
      PickResult result = delegate_picker_->Pick(args);
      // Intercept trailing metadata.
      if (result.type == PickResult::PICK_COMPLETE &&
          result.subchannel != nullptr) {
        new (args.call_state->Alloc(sizeof(TrailingMetadataHandler)))
            TrailingMetadataHandler(&result, cb_);
      }
      return result;
    }

   private:
    std::unique_ptr<SubchannelPicker> delegate_picker_;
    InterceptRecvTrailingMetadataCallback cb_;
  };

  class Helper : public ChannelControlHelper {
   public:
    Helper(
        RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy> parent,
        InterceptRecvTrailingMetadataCallback cb)
        : parent_(std::move(parent)), cb_(std::move(cb)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override {
      return parent_->channel_control_helper()->CreateSubchannel(
          std::move(address), args);
    }

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override {
      parent_->channel_control_helper()->UpdateState(
          state, status, absl::make_unique<Picker>(std::move(picker), cb_));
    }

    void RequestReresolution() override {
      parent_->channel_control_helper()->RequestReresolution();
    }

    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override {
      parent_->channel_control_helper()->AddTraceEvent(severity, message);
    }

   private:
    RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy> parent_;
    InterceptRecvTrailingMetadataCallback cb_;
  };

  class TrailingMetadataHandler {
   public:
    TrailingMetadataHandler(PickResult* result,
                            InterceptRecvTrailingMetadataCallback cb)
        : cb_(std::move(cb)) {
      result->recv_trailing_metadata_ready = [this](grpc_error_handle error,
                                                    MetadataInterface* metadata,
                                                    CallState* call_state) {
        RecordRecvTrailingMetadata(error, metadata, call_state);
      };
    }

   private:
    void RecordRecvTrailingMetadata(grpc_error_handle /*error*/,
                                    MetadataInterface* recv_trailing_metadata,
                                    CallState* call_state) {
      TrailingMetadataArgsSeen args_seen;
      args_seen.backend_metric_data = call_state->GetBackendMetricData();
      GPR_ASSERT(recv_trailing_metadata != nullptr);
      args_seen.metadata = CopyMetadataToVector(recv_trailing_metadata);
      cb_(args_seen);
      this->~TrailingMetadataHandler();
    }

    InterceptRecvTrailingMetadataCallback cb_;
  };
};

class InterceptTrailingConfig : public LoadBalancingPolicy::Config {
 public:
  const char* name() const override {
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

  const char* name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& /*json*/, grpc_error_handle* /*error*/) const override {
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
            absl::make_unique<Helper>(
                RefCountedPtr<AddressTestLoadBalancingPolicy>(this),
                std::move(cb)),
            std::move(args),
            /*delegate_policy_name=*/"pick_first",
            /*initial_refcount=*/2) {}

  ~AddressTestLoadBalancingPolicy() override = default;

  const char* name() const override { return kAddressTestLbPolicyName; }

 private:
  class Helper : public ChannelControlHelper {
   public:
    Helper(RefCountedPtr<AddressTestLoadBalancingPolicy> parent,
           AddressTestCallback cb)
        : parent_(std::move(parent)), cb_(std::move(cb)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        ServerAddress address, const grpc_channel_args& args) override {
      cb_(address);
      return parent_->channel_control_helper()->CreateSubchannel(
          std::move(address), args);
    }

    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     std::unique_ptr<SubchannelPicker> picker) override {
      parent_->channel_control_helper()->UpdateState(state, status,
                                                     std::move(picker));
    }

    void RequestReresolution() override {
      parent_->channel_control_helper()->RequestReresolution();
    }

    void AddTraceEvent(TraceSeverity severity,
                       absl::string_view message) override {
      parent_->channel_control_helper()->AddTraceEvent(severity, message);
    }

   private:
    RefCountedPtr<AddressTestLoadBalancingPolicy> parent_;
    AddressTestCallback cb_;
  };
};

class AddressTestConfig : public LoadBalancingPolicy::Config {
 public:
  const char* name() const override { return kAddressTestLbPolicyName; }
};

class AddressTestFactory : public LoadBalancingPolicyFactory {
 public:
  explicit AddressTestFactory(AddressTestCallback cb) : cb_(std::move(cb)) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<AddressTestLoadBalancingPolicy>(std::move(args), cb_);
  }

  const char* name() const override { return kAddressTestLbPolicyName; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& /*json*/, grpc_error_handle* /*error*/) const override {
    return MakeRefCounted<AddressTestConfig>();
  }

 private:
  AddressTestCallback cb_;
};

}  // namespace

void RegisterTestPickArgsLoadBalancingPolicy(TestPickArgsCallback cb,
                                             const char* delegate_policy_name) {
  LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
      absl::make_unique<TestPickArgsLbFactory>(std::move(cb),
                                               delegate_policy_name));
}

void RegisterInterceptRecvTrailingMetadataLoadBalancingPolicy(
    InterceptRecvTrailingMetadataCallback cb) {
  LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
      absl::make_unique<InterceptTrailingFactory>(std::move(cb)));
}

void RegisterAddressTestLoadBalancingPolicy(AddressTestCallback cb) {
  LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
      absl::make_unique<AddressTestFactory>(std::move(cb)));
}

}  // namespace grpc_core
