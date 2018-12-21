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

#include <string>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/transport/connectivity_state.h"

#ifndef GRPC_TEST_CORE_UTIL_FORWARDING_LOAD_BALANCING_POLICY_H
#define GRPC_TEST_CORE_UTIL_FORWARDING_LOAD_BALANCING_POLICY_H

namespace grpc_core {

extern TraceFlag grpc_trace_forwarding_lb;

// A minimal forwarding class to avoid implementing a standalone test LB.
class ForwardingLoadBalancingPolicy : public LoadBalancingPolicy {
 public:
  ForwardingLoadBalancingPolicy(const Args& args,
                                const std::string& delegate_policy_name)
      : LoadBalancingPolicy(args) {
    delegate_ =
        LoadBalancingPolicyRegistry::CreateLoadBalancingPolicy(
            delegate_policy_name.c_str(), args);
    grpc_pollset_set_add_pollset_set(delegate_->interested_parties(),
                                     interested_parties());
    // Give re-resolution closure to delegate.
    GRPC_CLOSURE_INIT(&on_delegate_request_reresolution_,
                      OnDelegateRequestReresolutionLocked, this,
                      grpc_combiner_scheduler(combiner()));
    Ref().release();  // held by callback.
    delegate_->SetReresolutionClosureLocked(&on_delegate_request_reresolution_);
  }

  const char* name() const override { return delegate_->name(); }

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
  void ShutdownLocked() override { delegate_.reset(); }

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

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_FORWARDING_LOAD_BALANCING_POLICY_H
