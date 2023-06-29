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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/endpoint_list.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_round_robin_trace(false, "round_robin");

namespace {

//
// round_robin LB policy
//

constexpr absl::string_view kRoundRobin = "round_robin";

class RoundRobin : public LoadBalancingPolicy {
 public:
  explicit RoundRobin(Args args);

  absl::string_view name() const override { return kRoundRobin; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ResetBackoffLocked() override;

 private:
  class RoundRobinEndpointList : public EndpointList {
   public:
    RoundRobinEndpointList(RefCountedPtr<RoundRobin> round_robin,
                           const EndpointAddressesList& endpoints,
                           const ChannelArgs& args)
        : EndpointList(std::move(round_robin),
                       GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)
                           ? "RoundRobinEndpointList"
                           : nullptr) {
      Init(endpoints, args,
           [&](RefCountedPtr<RoundRobinEndpointList> endpoint_list,
               const EndpointAddresses& addresses, const ChannelArgs& args) {
             return MakeOrphanable<RoundRobinEndpoint>(
                 std::move(endpoint_list), addresses, args,
                 policy<RoundRobin>()->work_serializer());
           });
    }

   private:
    class RoundRobinEndpoint : public Endpoint {
     public:
      RoundRobinEndpoint(RefCountedPtr<RoundRobinEndpointList> endpoint_list,
                         const EndpointAddresses& addresses,
                         const ChannelArgs& args,
                         std::shared_ptr<WorkSerializer> work_serializer)
          : Endpoint(std::move(endpoint_list)) {
        Init(addresses, args, std::move(work_serializer));
      }

     private:
      // Called when the child policy reports a connectivity state update.
      void OnStateUpdate(absl::optional<grpc_connectivity_state> old_state,
                         grpc_connectivity_state new_state,
                         const absl::Status& status) override;
    };

    LoadBalancingPolicy::ChannelControlHelper* channel_control_helper()
        const override {
      return policy<RoundRobin>()->channel_control_helper();
    }

    // Updates the counters of children in each state when a
    // child transitions from old_state to new_state.
    void UpdateStateCountersLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state);

    // Ensures that the right child list is used and then updates
    // the RR policy's connectivity state based on the child list's
    // state counters.
    void MaybeUpdateRoundRobinConnectivityStateLocked(
        absl::Status status_for_tf);

    std::string CountersString() const {
      return absl::StrCat("num_children=", size(), " num_ready=", num_ready_,
                          " num_connecting=", num_connecting_,
                          " num_transient_failure=", num_transient_failure_);
    }

    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;

    absl::Status last_failure_;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(RoundRobin* parent,
           std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>>
               pickers);

    PickResult Pick(PickArgs args) override;

   private:
    // Using pointer value only, no ref held -- do not dereference!
    RoundRobin* parent_;

    std::atomic<size_t> last_picked_index_;
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers_;
  };

  ~RoundRobin() override;

  void ShutdownLocked() override;

  // Current child list.
  OrphanablePtr<RoundRobinEndpointList> endpoint_list_;
  // Latest pending child list.
  // When we get an updated address list, we create a new child list
  // for it here, and we wait to swap it into endpoint_list_ until the new
  // list becomes READY.
  OrphanablePtr<RoundRobinEndpointList> latest_pending_endpoint_list_;

  bool shutdown_ = false;

  absl::BitGen bit_gen_;
};

//
// RoundRobin::Picker
//

RoundRobin::Picker::Picker(
    RoundRobin* parent,
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers)
    : parent_(parent), pickers_(std::move(pickers)) {
  // For discussion on why we generate a random starting index for
  // the picker, see https://github.com/grpc/grpc-go/issues/2580.
  size_t index = absl::Uniform<size_t>(parent->bit_gen_, 0, pickers_.size());
  last_picked_index_.store(index, std::memory_order_relaxed);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p picker %p] created picker from endpoint_list=%p "
            "with %" PRIuPTR " READY children; last_picked_index_=%" PRIuPTR,
            parent_, this, parent_->endpoint_list_.get(), pickers_.size(),
            index);
  }
}

RoundRobin::PickResult RoundRobin::Picker::Pick(PickArgs args) {
  size_t index = last_picked_index_.fetch_add(1, std::memory_order_relaxed) %
                 pickers_.size();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p picker %p] using picker index %" PRIuPTR ", picker=%p",
            parent_, this, index, pickers_[index].get());
  }
  return pickers_[index]->Pick(args);
}

//
// RoundRobin
//

RoundRobin::RoundRobin(Args args) : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Created", this);
  }
}

RoundRobin::~RoundRobin() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Destroying Round Robin policy", this);
  }
  GPR_ASSERT(endpoint_list_ == nullptr);
  GPR_ASSERT(latest_pending_endpoint_list_ == nullptr);
}

void RoundRobin::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Shutting down", this);
  }
  shutdown_ = true;
  endpoint_list_.reset();
  latest_pending_endpoint_list_.reset();
}

void RoundRobin::ResetBackoffLocked() {
  endpoint_list_->ResetBackoffLocked();
  if (latest_pending_endpoint_list_ != nullptr) {
    latest_pending_endpoint_list_->ResetBackoffLocked();
  }
}

absl::Status RoundRobin::UpdateLocked(UpdateArgs args) {
  EndpointAddressesList addresses;
  if (args.addresses.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] received update with %" PRIuPTR " addresses",
              this, args.addresses->size());
    }
    addresses = std::move(*args.addresses);
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] received update with address error: %s", this,
              args.addresses.status().ToString().c_str());
    }
    // If we already have a child list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (endpoint_list_ != nullptr) return args.addresses.status();
  }
  // Create new child list, replacing the previous pending list, if any.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
      latest_pending_endpoint_list_ != nullptr) {
    gpr_log(GPR_INFO, "[RR %p] replacing previous pending child list %p", this,
            latest_pending_endpoint_list_.get());
  }
  latest_pending_endpoint_list_ = MakeOrphanable<RoundRobinEndpointList>(
      Ref(DEBUG_LOCATION, "RoundRobinEndpointList"), std::move(addresses),
      args.args);
  // If the new list is empty, immediately promote it to
  // endpoint_list_ and report TRANSIENT_FAILURE.
  if (latest_pending_endpoint_list_->size() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
        endpoint_list_ != nullptr) {
      gpr_log(GPR_INFO, "[RR %p] replacing previous child list %p", this,
              endpoint_list_.get());
    }
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
    absl::Status status =
        args.addresses.ok() ? absl::UnavailableError(absl::StrCat(
                                  "empty address list: ", args.resolution_note))
                            : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    return status;
  }
  // Otherwise, if this is the initial update, immediately promote it to
  // endpoint_list_.
  if (endpoint_list_ == nullptr) {
    endpoint_list_ = std::move(latest_pending_endpoint_list_);
  }
  return absl::OkStatus();
}

//
// RoundRobin::RoundRobinEndpointList::RoundRobinEndpoint
//

void RoundRobin::RoundRobinEndpointList::RoundRobinEndpoint::OnStateUpdate(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state, const absl::Status& status) {
  auto* rr_endpoint_list = endpoint_list<RoundRobinEndpointList>();
  auto* round_robin = policy<RoundRobin>();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p] connectivity changed for child %p, endpoint_list %p "
            "(index %" PRIuPTR " of %" PRIuPTR
            "): prev_state=%s new_state=%s "
            "(%s)",
            round_robin, this, rr_endpoint_list, Index(),
            rr_endpoint_list->size(),
            (old_state.has_value() ? ConnectivityStateName(*old_state) : "N/A"),
            ConnectivityStateName(new_state), status.ToString().c_str());
  }
  if (new_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] child %p reported IDLE; requesting connection",
              round_robin, this);
    }
    ExitIdleLocked();
  }
  // If state changed, update state counters.
  if (!old_state.has_value() || *old_state != new_state) {
    rr_endpoint_list->UpdateStateCountersLocked(old_state, new_state);
  }
  // Update the policy state.
  rr_endpoint_list->MaybeUpdateRoundRobinConnectivityStateLocked(status);
}

//
// RoundRobin::RoundRobinEndpointList
//

void RoundRobin::RoundRobinEndpointList::UpdateStateCountersLocked(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  // We treat IDLE the same as CONNECTING, since it will immediately
  // transition into that state anyway.
  if (old_state.has_value()) {
    GPR_ASSERT(*old_state != GRPC_CHANNEL_SHUTDOWN);
    if (*old_state == GRPC_CHANNEL_READY) {
      GPR_ASSERT(num_ready_ > 0);
      --num_ready_;
    } else if (*old_state == GRPC_CHANNEL_CONNECTING ||
               *old_state == GRPC_CHANNEL_IDLE) {
      GPR_ASSERT(num_connecting_ > 0);
      --num_connecting_;
    } else if (*old_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      GPR_ASSERT(num_transient_failure_ > 0);
      --num_transient_failure_;
    }
  }
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  if (new_state == GRPC_CHANNEL_READY) {
    ++num_ready_;
  } else if (new_state == GRPC_CHANNEL_CONNECTING ||
             new_state == GRPC_CHANNEL_IDLE) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

void RoundRobin::RoundRobinEndpointList::
    MaybeUpdateRoundRobinConnectivityStateLocked(absl::Status status_for_tf) {
  auto* round_robin = policy<RoundRobin>();
  // If this is latest_pending_endpoint_list_, then swap it into
  // endpoint_list_ in the following cases:
  // - endpoint_list_ has no READY children.
  // - This list has at least one READY child and we have seen the
  //   initial connectivity state notification for all children.
  // - All of the children in this list are in TRANSIENT_FAILURE.
  //   (This may cause the channel to go from READY to TRANSIENT_FAILURE,
  //   but we're doing what the control plane told us to do.)
  if (round_robin->latest_pending_endpoint_list_.get() == this &&
      (round_robin->endpoint_list_->num_ready_ == 0 ||
       (num_ready_ > 0 && AllEndpointsSeenInitialState()) ||
       num_transient_failure_ == size())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      const std::string old_counters_string =
          round_robin->endpoint_list_ != nullptr
              ? round_robin->endpoint_list_->CountersString()
              : "";
      gpr_log(GPR_INFO,
              "[RR %p] swapping out child list %p (%s) in favor of %p (%s)",
              round_robin, round_robin->endpoint_list_.get(),
              old_counters_string.c_str(), this, CountersString().c_str());
    }
    round_robin->endpoint_list_ =
        std::move(round_robin->latest_pending_endpoint_list_);
  }
  // Only set connectivity state if this is the current child list.
  if (round_robin->endpoint_list_.get() != this) return;
  // First matching rule wins:
  // 1) ANY child is READY => policy is READY.
  // 2) ANY child is CONNECTING => policy is CONNECTING.
  // 3) ALL children are TRANSIENT_FAILURE => policy is TRANSIENT_FAILURE.
  if (num_ready_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] reporting READY with child list %p",
              round_robin, this);
    }
    std::vector<RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>> pickers;
    for (const auto& endpoint : endpoints()) {
      auto state = endpoint->connectivity_state();
      if (state.has_value() && *state == GRPC_CHANNEL_READY) {
        pickers.push_back(endpoint->picker());
      }
    }
    GPR_ASSERT(!pickers.empty());
    round_robin->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::OkStatus(),
        MakeRefCounted<Picker>(round_robin, std::move(pickers)));
  } else if (num_connecting_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] reporting CONNECTING with child list %p",
              round_robin, this);
    }
    round_robin->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        MakeRefCounted<QueuePicker>(nullptr));
  } else if (num_transient_failure_ == size()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] reporting TRANSIENT_FAILURE with child list %p: %s",
              round_robin, this, status_for_tf.ToString().c_str());
    }
    if (!status_for_tf.ok()) {
      last_failure_ = absl::UnavailableError(
          absl::StrCat("connections to all backends failing; last error: ",
                       status_for_tf.message()));
    }
    round_robin->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, last_failure_,
        MakeRefCounted<TransientFailurePicker>(last_failure_));
  }
}

//
// factory
//

class RoundRobinConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kRoundRobin; }
};

class RoundRobinFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<RoundRobin>(std::move(args));
  }

  absl::string_view name() const override { return kRoundRobin; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<RoundRobinConfig>();
  }
};

}  // namespace

void RegisterRoundRobinLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<RoundRobinFactory>());
}

}  // namespace grpc_core
