/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "test/core/util/test_lb_policies.h"

#include <string>

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

TraceFlag grpc_trace_forwarding_lb(false, "forwarding_lb");

namespace {

//
// ForwardingLoadBalancingPolicy
//

// A minimal forwarding class to avoid implementing a standalone test LB.
class ForwardingLoadBalancingPolicy : public LoadBalancingPolicy {
 public:
  ForwardingLoadBalancingPolicy(
      UniquePtr<ChannelControlHelper> delegating_helper, Args args,
      const std::string& delegate_policy_name, intptr_t initial_refcount = 1)
      : LoadBalancingPolicy(std::move(args), initial_refcount) {
    Args delegate_args;
    delegate_args.combiner = combiner();
    delegate_args.channel_control_helper = std::move(delegating_helper);
    delegate_args.args = args.args;
    delegate_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
        delegate_policy_name.c_str(), std::move(delegate_args));
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
  }

  ~ForwardingLoadBalancingPolicy() override = default;

  void UpdateLocked(const grpc_channel_args& args,
                    RefCountedPtr<Config> lb_config) override {
    delegate_->UpdateLocked(args, std::move(lb_config));
  }

  void ExitIdleLocked() override { delegate_->ExitIdleLocked(); }

  void ResetBackoffLocked() override { delegate_->ResetBackoffLocked(); }

  void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) override {
    delegate_->FillChildRefsForChannelz(child_subchannels, child_channels);
  }

 private:
  void ShutdownLocked() override { delegate_.reset(); }

  OrphanablePtr<LoadBalancingPolicy> delegate_;
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
      Args args, InterceptRecvTrailingMetadataCallback cb, void* user_data)
      : ForwardingLoadBalancingPolicy(
            UniquePtr<ChannelControlHelper>(New<Helper>(
                RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy>(
                    this),
                cb, user_data)),
            std::move(args),
            /*delegate_lb_policy_name=*/"pick_first",
            /*initial_refcount=*/2) {}

  ~InterceptRecvTrailingMetadataLoadBalancingPolicy() override = default;

  const char* name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

 private:
  class Picker : public SubchannelPicker {
   public:
    explicit Picker(UniquePtr<SubchannelPicker> delegate_picker,
                    InterceptRecvTrailingMetadataCallback cb, void* user_data)
        : delegate_picker_(std::move(delegate_picker)),
          cb_(cb),
          user_data_(user_data) {}

    PickResult Pick(PickArgs* pick, grpc_error** error) override {
      PickResult result = delegate_picker_->Pick(pick, error);
      if (result == PICK_COMPLETE && pick->connected_subchannel != nullptr) {
        New<TrailingMetadataHandler>(pick, cb_, user_data_);  // deletes itself
      }
      return result;
    }

   private:
    UniquePtr<SubchannelPicker> delegate_picker_;
    InterceptRecvTrailingMetadataCallback cb_;
    void* user_data_;
  };

  class Helper : public ChannelControlHelper {
   public:
    Helper(
        RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy> parent,
        InterceptRecvTrailingMetadataCallback cb, void* user_data)
        : parent_(std::move(parent)), cb_(cb), user_data_(user_data) {}

    Subchannel* CreateSubchannel(const grpc_channel_args& args) override {
      return parent_->channel_control_helper()->CreateSubchannel(args);
    }

    grpc_channel* CreateChannel(const char* target,
                                const grpc_channel_args& args) override {
      return parent_->channel_control_helper()->CreateChannel(target, args);
    }

    void UpdateState(grpc_connectivity_state state, grpc_error* state_error,
                     UniquePtr<SubchannelPicker> picker) override {
      parent_->channel_control_helper()->UpdateState(
          state, state_error,
          UniquePtr<SubchannelPicker>(
              New<Picker>(std::move(picker), cb_, user_data_)));
    }

    void RequestReresolution() override {
      parent_->channel_control_helper()->RequestReresolution();
    }

   private:
    RefCountedPtr<InterceptRecvTrailingMetadataLoadBalancingPolicy> parent_;
    InterceptRecvTrailingMetadataCallback cb_;
    void* user_data_;
  };

  class TrailingMetadataHandler {
   public:
    TrailingMetadataHandler(PickArgs* pick,
                            InterceptRecvTrailingMetadataCallback cb,
                            void* user_data)
        : cb_(cb), user_data_(user_data) {
      GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                        RecordRecvTrailingMetadata, this,
                        grpc_schedule_on_exec_ctx);
      pick->recv_trailing_metadata_ready = &recv_trailing_metadata_ready_;
      pick->original_recv_trailing_metadata_ready =
          &original_recv_trailing_metadata_ready_;
      pick->recv_trailing_metadata = &recv_trailing_metadata_;
    }

   private:
    static void RecordRecvTrailingMetadata(void* arg, grpc_error* err) {
      TrailingMetadataHandler* self =
          static_cast<TrailingMetadataHandler*>(arg);
      GPR_ASSERT(self->recv_trailing_metadata_ != nullptr);
      self->cb_(self->user_data_);
      GRPC_CLOSURE_SCHED(self->original_recv_trailing_metadata_ready_,
                         GRPC_ERROR_REF(err));
      Delete(self);
    }

    InterceptRecvTrailingMetadataCallback cb_;
    void* user_data_;
    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
    grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  };
};

class InterceptTrailingFactory : public LoadBalancingPolicyFactory {
 public:
  explicit InterceptTrailingFactory(InterceptRecvTrailingMetadataCallback cb,
                                    void* user_data)
      : cb_(cb), user_data_(user_data) {}

  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return OrphanablePtr<LoadBalancingPolicy>(
        New<InterceptRecvTrailingMetadataLoadBalancingPolicy>(std::move(args),
                                                              cb_, user_data_));
  }

  const char* name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

 private:
  InterceptRecvTrailingMetadataCallback cb_;
  void* user_data_;
};

}  // namespace

void RegisterInterceptRecvTrailingMetadataLoadBalancingPolicy(
    InterceptRecvTrailingMetadataCallback cb, void* user_data) {
  LoadBalancingPolicyRegistry::Builder::RegisterLoadBalancingPolicyFactory(
      UniquePtr<LoadBalancingPolicyFactory>(
          New<InterceptTrailingFactory>(cb, user_data)));
}

}  // namespace grpc_core
