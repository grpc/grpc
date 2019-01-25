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
  ForwardingLoadBalancingPolicy(Args args,
                                const std::string& delegate_policy_name)
      : LoadBalancingPolicy(std::move(args)) {
    Args delegate_args;
    delegate_args.combiner = combiner();
    delegate_args.client_channel_factory = client_channel_factory();
    delegate_args.subchannel_pool = subchannel_pool()->Ref();
    delegate_args.args = args.args;
    delegate_args.lb_config = args.lb_config;
    delegate_ = LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
        delegate_policy_name.c_str(), std::move(delegate_args));
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
    // Give re-resolution closure to delegate.
    GRPC_CLOSURE_INIT(&on_delegate_request_reresolution_,
                      OnDelegateRequestReresolutionLocked, this,
                      grpc_combiner_scheduler(combiner()));
    Ref().release();  // held by callback.
    delegate_->SetReresolutionClosureLocked(&on_delegate_request_reresolution_);
  }

  ~ForwardingLoadBalancingPolicy() override = default;

  void UpdateLocked(const grpc_channel_args& args,
                    grpc_json* lb_config) override {
    delegate_->UpdateLocked(args, lb_config);
  }

  bool PickLocked(PickState* pick, grpc_error** error) override {
    return delegate_->PickLocked(pick, error);
  }

  void CancelPickLocked(PickState* pick, grpc_error* error) override {
    delegate_->CancelPickLocked(pick, error);
  }

  void CancelMatchingPicksLocked(uint32_t initial_metadata_flags_mask,
                                 uint32_t initial_metadata_flags_eq,
                                 grpc_error* error) override {
    delegate_->CancelMatchingPicksLocked(initial_metadata_flags_mask,
                                         initial_metadata_flags_eq, error);
  }

  void NotifyOnStateChangeLocked(grpc_connectivity_state* state,
                                 grpc_closure* closure) override {
    delegate_->NotifyOnStateChangeLocked(state, closure);
  }

  grpc_connectivity_state CheckConnectivityLocked(
      grpc_error** connectivity_error) override {
    return delegate_->CheckConnectivityLocked(connectivity_error);
  }

  void HandOffPendingPicksLocked(LoadBalancingPolicy* new_policy) override {
    delegate_->HandOffPendingPicksLocked(new_policy);
  }

  void ExitIdleLocked() override { delegate_->ExitIdleLocked(); }

  void ResetBackoffLocked() override { delegate_->ResetBackoffLocked(); }

  void FillChildRefsForChannelz(
      channelz::ChildRefsList* child_subchannels,
      channelz::ChildRefsList* child_channels) override {
    delegate_->FillChildRefsForChannelz(child_subchannels, child_channels);
  }

 private:
  void ShutdownLocked() override {
    delegate_.reset();
    TryReresolutionLocked(&grpc_trace_forwarding_lb, GRPC_ERROR_CANCELLED);
  }

  static void OnDelegateRequestReresolutionLocked(void* arg,
                                                  grpc_error* error) {
    ForwardingLoadBalancingPolicy* self =
        static_cast<ForwardingLoadBalancingPolicy*>(arg);
    if (error != GRPC_ERROR_NONE || self->delegate_ == nullptr) {
      self->Unref();
      return;
    }
    self->TryReresolutionLocked(&grpc_trace_forwarding_lb, GRPC_ERROR_NONE);
    self->delegate_->SetReresolutionClosureLocked(
        &self->on_delegate_request_reresolution_);
  }

  OrphanablePtr<LoadBalancingPolicy> delegate_;
  grpc_closure on_delegate_request_reresolution_;
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
      : ForwardingLoadBalancingPolicy(std::move(args),
                                      /*delegate_lb_policy_name=*/"pick_first"),
        cb_(cb),
        user_data_(user_data) {}

  ~InterceptRecvTrailingMetadataLoadBalancingPolicy() override = default;

  const char* name() const override {
    return kInterceptRecvTrailingMetadataLbPolicyName;
  }

  bool PickLocked(PickState* pick, grpc_error** error) override {
    bool ret = ForwardingLoadBalancingPolicy::PickLocked(pick, error);
    // Note: This assumes that the delegate policy does not
    // intercepting recv_trailing_metadata.  If we ever need to use
    // this with a delegate policy that does, then we'll need to
    // handle async pick returns separately.
    New<TrailingMetadataHandler>(pick, cb_, user_data_);  // deletes itself
    return ret;
  }

 private:
  class TrailingMetadataHandler {
   public:
    TrailingMetadataHandler(PickState* pick,
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

  InterceptRecvTrailingMetadataCallback cb_;
  void* user_data_;
};

class InterceptTrailingFactory : public LoadBalancingPolicyFactory {
 public:
  explicit InterceptTrailingFactory(InterceptRecvTrailingMetadataCallback cb,
                                    void* user_data)
      : cb_(cb), user_data_(user_data) {}

  grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy>
  CreateLoadBalancingPolicy(
      grpc_core::LoadBalancingPolicy::Args args) const override {
    return grpc_core::OrphanablePtr<grpc_core::LoadBalancingPolicy>(
        grpc_core::New<InterceptRecvTrailingMetadataLoadBalancingPolicy>(
            std::move(args), cb_, user_data_));
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
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          grpc_core::UniquePtr<grpc_core::LoadBalancingPolicyFactory>(
              grpc_core::New<InterceptTrailingFactory>(cb, user_data)));
}

}  // namespace grpc_core
