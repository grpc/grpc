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

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
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
  ~RoundRobin() override;

  // Forward declaration.
  class RoundRobinSubchannelList;

  // Data for a particular subchannel in a subchannel list.
  // This subclass adds the following functionality:
  // - Tracks the previous connectivity state of the subchannel, so that
  //   we know how many subchannels are in each state.
  class RoundRobinSubchannelData
      : public SubchannelData<RoundRobinSubchannelList,
                              RoundRobinSubchannelData> {
   public:
    RoundRobinSubchannelData(
        SubchannelList<RoundRobinSubchannelList, RoundRobinSubchannelData>*
            subchannel_list,
        const ServerAddress& address,
        RefCountedPtr<SubchannelInterface> subchannel)
        : SubchannelData(subchannel_list, address, std::move(subchannel)) {}

    absl::optional<grpc_connectivity_state> connectivity_state() const {
      return logical_connectivity_state_;
    }

   private:
    // Performs connectivity state updates that need to be done only
    // after we have started watching.
    void ProcessConnectivityChangeLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state) override;

    // Updates the logical connectivity state.
    void UpdateLogicalConnectivityStateLocked(
        grpc_connectivity_state connectivity_state);

    // The logical connectivity state of the subchannel.
    // Note that the logical connectivity state may differ from the
    // actual reported state in some cases (e.g., after we see
    // TRANSIENT_FAILURE, we ignore any subsequent state changes until
    // we see READY).
    absl::optional<grpc_connectivity_state> logical_connectivity_state_;
  };

  // A list of subchannels.
  class RoundRobinSubchannelList
      : public SubchannelList<RoundRobinSubchannelList,
                              RoundRobinSubchannelData> {
   public:
    RoundRobinSubchannelList(RoundRobin* policy, ServerAddressList addresses,
                             const ChannelArgs& args)
        : SubchannelList(policy,
                         (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)
                              ? "RoundRobinSubchannelList"
                              : nullptr),
                         std::move(addresses), policy->channel_control_helper(),
                         args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~RoundRobinSubchannelList() override {
      RoundRobin* p = static_cast<RoundRobin*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    // Updates the counters of subchannels in each state when a
    // subchannel transitions from old_state to new_state.
    void UpdateStateCountersLocked(
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state);

    // Ensures that the right subchannel list is used and then updates
    // the RR policy's connectivity state based on the subchannel list's
    // state counters.
    void MaybeUpdateRoundRobinConnectivityStateLocked(
        absl::Status status_for_tf);

   private:
    std::string CountersString() const {
      return absl::StrCat("num_subchannels=", num_subchannels(),
                          " num_ready=", num_ready_,
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
    Picker(RoundRobin* parent, RoundRobinSubchannelList* subchannel_list);

    PickResult Pick(PickArgs args) override;

   private:
    // Using pointer value only, no ref held -- do not dereference!
    RoundRobin* parent_;

    size_t last_picked_index_;
    std::vector<RefCountedPtr<SubchannelInterface>> subchannels_;
  };

  void ShutdownLocked() override;

  // List of subchannels.
  RefCountedPtr<RoundRobinSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  // When we get an updated address list, we create a new subchannel list
  // for it here, and we wait to swap it into subchannel_list_ until the new
  // list becomes READY.
  RefCountedPtr<RoundRobinSubchannelList> latest_pending_subchannel_list_;

  bool shutdown_ = false;
};

//
// RoundRobin::Picker
//

RoundRobin::Picker::Picker(RoundRobin* parent,
                           RoundRobinSubchannelList* subchannel_list)
    : parent_(parent) {
  for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
    RoundRobinSubchannelData* sd = subchannel_list->subchannel(i);
    if (sd->connectivity_state().value_or(GRPC_CHANNEL_IDLE) ==
        GRPC_CHANNEL_READY) {
      subchannels_.push_back(sd->subchannel()->Ref());
    }
  }
  // For discussion on why we generate a random starting index for
  // the picker, see https://github.com/grpc/grpc-go/issues/2580.
  // TODO(roth): rand(3) is not thread-safe.  This should be replaced with
  // something better as part of https://github.com/grpc/grpc/issues/17891.
  last_picked_index_ = rand() % subchannels_.size();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p picker %p] created picker from subchannel_list=%p "
            "with %" PRIuPTR " READY subchannels; last_picked_index_=%" PRIuPTR,
            parent_, this, subchannel_list, subchannels_.size(),
            last_picked_index_);
  }
}

RoundRobin::PickResult RoundRobin::Picker::Pick(PickArgs /*args*/) {
  last_picked_index_ = (last_picked_index_ + 1) % subchannels_.size();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO,
            "[RR %p picker %p] returning index %" PRIuPTR ", subchannel=%p",
            parent_, this, last_picked_index_,
            subchannels_[last_picked_index_].get());
  }
  return PickResult::Complete(subchannels_[last_picked_index_]);
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
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
}

void RoundRobin::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(GPR_INFO, "[RR %p] Shutting down", this);
  }
  shutdown_ = true;
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
}

void RoundRobin::ResetBackoffLocked() {
  subchannel_list_->ResetBackoffLocked();
  if (latest_pending_subchannel_list_ != nullptr) {
    latest_pending_subchannel_list_->ResetBackoffLocked();
  }
}

absl::Status RoundRobin::UpdateLocked(UpdateArgs args) {
  ServerAddressList addresses;
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
    // If we already have a subchannel list, then keep using the existing
    // list, but still report back that the update was not accepted.
    if (subchannel_list_ != nullptr) return args.addresses.status();
  }
  // Create new subchannel list, replacing the previous pending list, if any.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
      latest_pending_subchannel_list_ != nullptr) {
    gpr_log(GPR_INFO, "[RR %p] replacing previous pending subchannel list %p",
            this, latest_pending_subchannel_list_.get());
  }
  latest_pending_subchannel_list_ = MakeRefCounted<RoundRobinSubchannelList>(
      this, std::move(addresses), args.args);
  latest_pending_subchannel_list_->StartWatchingLocked();
  // If the new list is empty, immediately promote it to
  // subchannel_list_ and report TRANSIENT_FAILURE.
  if (latest_pending_subchannel_list_->num_subchannels() == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
        subchannel_list_ != nullptr) {
      gpr_log(GPR_INFO, "[RR %p] replacing previous subchannel list %p", this,
              subchannel_list_.get());
    }
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
    absl::Status status =
        args.addresses.ok() ? absl::UnavailableError(absl::StrCat(
                                  "empty address list: ", args.resolution_note))
                            : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<TransientFailurePicker>(status));
    return status;
  }
  // Otherwise, if this is the initial update, immediately promote it to
  // subchannel_list_ and report CONNECTING.
  if (subchannel_list_.get() == nullptr) {
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        absl::make_unique<QueuePicker>(Ref(DEBUG_LOCATION, "QueuePicker")));
  }
  return absl::OkStatus();
}

//
// RoundRobinSubchannelList
//

void RoundRobin::RoundRobinSubchannelList::UpdateStateCountersLocked(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  if (old_state.has_value()) {
    GPR_ASSERT(*old_state != GRPC_CHANNEL_SHUTDOWN);
    if (*old_state == GRPC_CHANNEL_READY) {
      GPR_ASSERT(num_ready_ > 0);
      --num_ready_;
    } else if (*old_state == GRPC_CHANNEL_CONNECTING) {
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
  } else if (new_state == GRPC_CHANNEL_CONNECTING) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

void RoundRobin::RoundRobinSubchannelList::
    MaybeUpdateRoundRobinConnectivityStateLocked(absl::Status status_for_tf) {
  RoundRobin* p = static_cast<RoundRobin*>(policy());
  // If this is latest_pending_subchannel_list_, then swap it into
  // subchannel_list_ in the following cases:
  // - subchannel_list_ has no READY subchannels.
  // - This list has at least one READY subchannel.
  // - All of the subchannels in this list are in TRANSIENT_FAILURE.
  //   (This may cause the channel to go from READY to TRANSIENT_FAILURE,
  //   but we're doing what the control plane told us to do.)
  if (p->latest_pending_subchannel_list_.get() == this &&
      (p->subchannel_list_->num_ready_ == 0 || num_ready_ > 0 ||
       num_transient_failure_ == num_subchannels())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      const std::string old_counters_string =
          p->subchannel_list_ != nullptr ? p->subchannel_list_->CountersString()
                                         : "";
      gpr_log(
          GPR_INFO,
          "[RR %p] swapping out subchannel list %p (%s) in favor of %p (%s)", p,
          p->subchannel_list_.get(), old_counters_string.c_str(), this,
          CountersString().c_str());
    }
    p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
  }
  // Only set connectivity state if this is the current subchannel list.
  if (p->subchannel_list_.get() != this) return;
  // First matching rule wins:
  // 1) ANY subchannel is READY => policy is READY.
  // 2) ANY subchannel is CONNECTING => policy is CONNECTING.
  // 3) ALL subchannels are TRANSIENT_FAILURE => policy is TRANSIENT_FAILURE.
  if (num_ready_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] reporting READY with subchannel list %p", p,
              this);
    }
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::Status(), absl::make_unique<Picker>(p, this));
  } else if (num_connecting_ > 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO, "[RR %p] reporting CONNECTING with subchannel list %p",
              p, this);
    }
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        absl::make_unique<QueuePicker>(p->Ref(DEBUG_LOCATION, "QueuePicker")));
  } else if (num_transient_failure_ == num_subchannels()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] reporting TRANSIENT_FAILURE with subchannel list %p: %s",
              p, this, status_for_tf.ToString().c_str());
    }
    if (!status_for_tf.ok()) {
      last_failure_ = absl::UnavailableError(
          absl::StrCat("connections to all backends failing; last error: ",
                       status_for_tf.ToString()));
    }
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, last_failure_,
        absl::make_unique<TransientFailurePicker>(last_failure_));
  }
}

//
// RoundRobinSubchannelData
//

void RoundRobin::RoundRobinSubchannelData::ProcessConnectivityChangeLocked(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  RoundRobin* p = static_cast<RoundRobin*>(subchannel_list()->policy());
  GPR_ASSERT(subchannel() != nullptr);
  // If this is not the initial state notification and the new state is
  // TRANSIENT_FAILURE or IDLE, re-resolve.
  // Note that we don't want to do this on the initial state notification,
  // because that would result in an endless loop of re-resolution.
  if (old_state.has_value() && (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
                                new_state == GRPC_CHANNEL_IDLE)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] Subchannel %p reported %s; requesting re-resolution", p,
              subchannel(), ConnectivityStateName(new_state));
    }
    p->channel_control_helper()->RequestReresolution();
  }
  if (new_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] Subchannel %p reported IDLE; requesting connection", p,
              subchannel());
    }
    subchannel()->RequestConnection();
  }
  // Update logical connectivity state.
  UpdateLogicalConnectivityStateLocked(new_state);
  // Update the policy state.
  subchannel_list()->MaybeUpdateRoundRobinConnectivityStateLocked(
      connectivity_status());
}

void RoundRobin::RoundRobinSubchannelData::UpdateLogicalConnectivityStateLocked(
    grpc_connectivity_state connectivity_state) {
  RoundRobin* p = static_cast<RoundRobin*>(subchannel_list()->policy());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(
        GPR_INFO,
        "[RR %p] connectivity changed for subchannel %p, subchannel_list %p "
        "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s",
        p, subchannel(), subchannel_list(), Index(),
        subchannel_list()->num_subchannels(),
        (logical_connectivity_state_.has_value()
             ? ConnectivityStateName(*logical_connectivity_state_)
             : "N/A"),
        ConnectivityStateName(connectivity_state));
  }
  // Decide what state to report for aggregation purposes.
  // If the last logical state was TRANSIENT_FAILURE, then ignore the
  // state change unless the new state is READY.
  if (logical_connectivity_state_.has_value() &&
      *logical_connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE &&
      connectivity_state != GRPC_CHANNEL_READY) {
    return;
  }
  // If the new state is IDLE, treat it as CONNECTING, since it will
  // immediately transition into CONNECTING anyway.
  if (connectivity_state == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] subchannel %p, subchannel_list %p (index %" PRIuPTR
              " of %" PRIuPTR "): treating IDLE as CONNECTING",
              p, subchannel(), subchannel_list(), Index(),
              subchannel_list()->num_subchannels());
    }
    connectivity_state = GRPC_CHANNEL_CONNECTING;
  }
  // If no change, return false.
  if (logical_connectivity_state_.has_value() &&
      *logical_connectivity_state_ == connectivity_state) {
    return;
  }
  // Otherwise, update counters and logical state.
  subchannel_list()->UpdateStateCountersLocked(logical_connectivity_state_,
                                               connectivity_state);
  logical_connectivity_state_ = connectivity_state;
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
      absl::make_unique<RoundRobinFactory>());
}

}  // namespace grpc_core
