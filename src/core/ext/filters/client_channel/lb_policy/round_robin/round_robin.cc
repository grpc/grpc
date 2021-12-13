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

#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

TraceFlag grpc_lb_round_robin_trace(false, "round_robin");

namespace {

//
// round_robin LB policy
//

constexpr char kRoundRobin[] = "round_robin";

class RoundRobin : public LoadBalancingPolicy {
 public:
  explicit RoundRobin(Args args);

  const char* name() const override { return kRoundRobin; }

  void UpdateLocked(UpdateArgs args) override;
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

    grpc_connectivity_state connectivity_state() const {
      return last_connectivity_state_;
    }

    bool seen_failure_since_ready() const { return seen_failure_since_ready_; }

    // Performs connectivity state updates that need to be done both when we
    // first start watching and when a watcher notification is received.
    void UpdateConnectivityStateLocked(
        grpc_connectivity_state connectivity_state);

   private:
    // Performs connectivity state updates that need to be done only
    // after we have started watching.
    void ProcessConnectivityChangeLocked(
        grpc_connectivity_state connectivity_state) override;

    grpc_connectivity_state last_connectivity_state_ = GRPC_CHANNEL_IDLE;
    bool seen_failure_since_ready_ = false;
  };

  // A list of subchannels.
  class RoundRobinSubchannelList
      : public SubchannelList<RoundRobinSubchannelList,
                              RoundRobinSubchannelData> {
   public:
    RoundRobinSubchannelList(RoundRobin* policy, TraceFlag* tracer,
                             ServerAddressList addresses,
                             const grpc_channel_args& args)
        : SubchannelList(policy, tracer, std::move(addresses),
                         policy->channel_control_helper(), args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~RoundRobinSubchannelList() override {
      RoundRobin* p = static_cast<RoundRobin*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    // Starts watching the subchannels in this list.
    void StartWatchingLocked();

    // Updates the counters of subchannels in each state when a
    // subchannel transitions from old_state to new_state.
    void UpdateStateCountersLocked(grpc_connectivity_state old_state,
                                   grpc_connectivity_state new_state);

    // If this subchannel list is the RR policy's current subchannel
    // list, updates the RR policy's connectivity state based on the
    // subchannel list's state counters.
    void MaybeUpdateRoundRobinConnectivityStateLocked();

    // Updates the RR policy's overall state based on the counters of
    // subchannels in each state.
    void UpdateRoundRobinStateFromSubchannelStateCountsLocked();

   private:
    size_t num_ready_ = 0;
    size_t num_connecting_ = 0;
    size_t num_transient_failure_ = 0;
  };

  class Picker : public SubchannelPicker {
   public:
    Picker(RoundRobin* parent, RoundRobinSubchannelList* subchannel_list);

    PickResult Pick(PickArgs args) override;

   private:
    // Using pointer value only, no ref held -- do not dereference!
    RoundRobin* parent_;

    size_t last_picked_index_;
    absl::InlinedVector<RefCountedPtr<SubchannelInterface>, 10> subchannels_;
  };

  void ShutdownLocked() override;

  // List of subchannels.
  OrphanablePtr<RoundRobinSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  // When we get an updated address list, we create a new subchannel list
  // for it here, and we wait to swap it into subchannel_list_ until the new
  // list becomes READY.
  OrphanablePtr<RoundRobinSubchannelList> latest_pending_subchannel_list_;

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
    if (sd->connectivity_state() == GRPC_CHANNEL_READY) {
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

void RoundRobin::RoundRobinSubchannelList::StartWatchingLocked() {
  if (num_subchannels() == 0) return;
  // Check current state of each subchannel synchronously, since any
  // subchannel already used by some other channel may have a non-IDLE
  // state.
  for (size_t i = 0; i < num_subchannels(); ++i) {
    grpc_connectivity_state state =
        subchannel(i)->CheckConnectivityStateLocked();
    if (state != GRPC_CHANNEL_IDLE) {
      subchannel(i)->UpdateConnectivityStateLocked(state);
    }
  }
  // Start connectivity watch for each subchannel.
  for (size_t i = 0; i < num_subchannels(); i++) {
    if (subchannel(i)->subchannel() != nullptr) {
      subchannel(i)->StartConnectivityWatchLocked();
      subchannel(i)->subchannel()->AttemptToConnect();
    }
  }
  // Now set the LB policy's state based on the subchannels' states.
  UpdateRoundRobinStateFromSubchannelStateCountsLocked();
}

void RoundRobin::RoundRobinSubchannelList::UpdateStateCountersLocked(
    grpc_connectivity_state old_state, grpc_connectivity_state new_state) {
  GPR_ASSERT(old_state != GRPC_CHANNEL_SHUTDOWN);
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  if (old_state == GRPC_CHANNEL_READY) {
    GPR_ASSERT(num_ready_ > 0);
    --num_ready_;
  } else if (old_state == GRPC_CHANNEL_CONNECTING) {
    GPR_ASSERT(num_connecting_ > 0);
    --num_connecting_;
  } else if (old_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    GPR_ASSERT(num_transient_failure_ > 0);
    --num_transient_failure_;
  }
  if (new_state == GRPC_CHANNEL_READY) {
    ++num_ready_;
  } else if (new_state == GRPC_CHANNEL_CONNECTING) {
    ++num_connecting_;
  } else if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    ++num_transient_failure_;
  }
}

// Sets the RR policy's connectivity state and generates a new picker based
// on the current subchannel list.
void RoundRobin::RoundRobinSubchannelList::
    MaybeUpdateRoundRobinConnectivityStateLocked() {
  RoundRobin* p = static_cast<RoundRobin*>(policy());
  // Only set connectivity state if this is the current subchannel list.
  if (p->subchannel_list_.get() != this) return;
  // In priority order. The first rule to match terminates the search (ie, if we
  // are on rule n, all previous rules were unfulfilled).
  //
  // 1) RULE: ANY subchannel is READY => policy is READY.
  //    CHECK: subchannel_list->num_ready > 0.
  //
  // 2) RULE: ANY subchannel is CONNECTING => policy is CONNECTING.
  //    CHECK: sd->curr_connectivity_state == CONNECTING.
  //
  // 3) RULE: ALL subchannels are TRANSIENT_FAILURE => policy is
  //                                                   TRANSIENT_FAILURE.
  //    CHECK: subchannel_list->num_transient_failures ==
  //           subchannel_list->num_subchannels.
  if (num_ready_ > 0) {
    // 1) READY
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_READY, absl::Status(), absl::make_unique<Picker>(p, this));
  } else if (num_connecting_ > 0) {
    // 2) CONNECTING
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_CONNECTING, absl::Status(),
        absl::make_unique<QueuePicker>(p->Ref(DEBUG_LOCATION, "QueuePicker")));
  } else if (num_transient_failure_ == num_subchannels()) {
    // 3) TRANSIENT_FAILURE
    absl::Status status =
        absl::UnavailableError("connections to all backends failing");
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<TransientFailurePicker>(status));
  }
}

void RoundRobin::RoundRobinSubchannelList::
    UpdateRoundRobinStateFromSubchannelStateCountsLocked() {
  RoundRobin* p = static_cast<RoundRobin*>(policy());
  // If we have at least one READY subchannel, then swap to the new list.
  // Also, if all of the subchannels are in TRANSIENT_FAILURE, then we know
  // we've tried all of them and failed, so we go ahead and swap over
  // anyway; this may cause the channel to go from READY to TRANSIENT_FAILURE,
  // but we are doing what the control plane told us to do.
  if (num_ready_ > 0 || num_transient_failure_ == num_subchannels()) {
    if (p->subchannel_list_.get() != this) {
      // Promote this list to p->subchannel_list_.
      // This list must be p->latest_pending_subchannel_list_, because
      // any previous update would have been shut down already and
      // therefore we would not be receiving a notification for them.
      GPR_ASSERT(p->latest_pending_subchannel_list_.get() == this);
      GPR_ASSERT(!shutting_down());
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
        const size_t old_num_subchannels =
            p->subchannel_list_ != nullptr
                ? p->subchannel_list_->num_subchannels()
                : 0;
        gpr_log(GPR_INFO,
                "[RR %p] phasing out subchannel list %p (size %" PRIuPTR
                ") in favor of %p (size %" PRIuPTR ")",
                p, p->subchannel_list_.get(), old_num_subchannels, this,
                num_subchannels());
      }
      p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
    }
  }
  // Update the RR policy's connectivity state if needed.
  MaybeUpdateRoundRobinConnectivityStateLocked();
}

void RoundRobin::RoundRobinSubchannelData::UpdateConnectivityStateLocked(
    grpc_connectivity_state connectivity_state) {
  RoundRobin* p = static_cast<RoundRobin*>(subchannel_list()->policy());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
    gpr_log(
        GPR_INFO,
        "[RR %p] connectivity changed for subchannel %p, subchannel_list %p "
        "(index %" PRIuPTR " of %" PRIuPTR "): prev_state=%s new_state=%s",
        p, subchannel(), subchannel_list(), Index(),
        subchannel_list()->num_subchannels(),
        ConnectivityStateName(last_connectivity_state_),
        ConnectivityStateName(connectivity_state));
  }
  // Decide what state to report for aggregation purposes.
  // If we haven't seen a failure since the last time we were in state
  // READY, then we report the state change as-is.  However, once we do see
  // a failure, we report TRANSIENT_FAILURE and do not report any subsequent
  // state changes until we go back into state READY.
  if (!seen_failure_since_ready_) {
    if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      seen_failure_since_ready_ = true;
    }
    subchannel_list()->UpdateStateCountersLocked(last_connectivity_state_,
                                                 connectivity_state);
  } else {
    if (connectivity_state == GRPC_CHANNEL_READY) {
      seen_failure_since_ready_ = false;
      subchannel_list()->UpdateStateCountersLocked(
          GRPC_CHANNEL_TRANSIENT_FAILURE, connectivity_state);
    }
  }
  // Record last seen connectivity state.
  last_connectivity_state_ = connectivity_state;
}

void RoundRobin::RoundRobinSubchannelData::ProcessConnectivityChangeLocked(
    grpc_connectivity_state connectivity_state) {
  RoundRobin* p = static_cast<RoundRobin*>(subchannel_list()->policy());
  GPR_ASSERT(subchannel() != nullptr);
  // If the new state is TRANSIENT_FAILURE, re-resolve.
  // Only do this if we've started watching, not at startup time.
  // Otherwise, if the subchannel was already in state TRANSIENT_FAILURE
  // when the subchannel list was created, we'd wind up in a constant
  // loop of re-resolution.
  // Also attempt to reconnect.
  if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace)) {
      gpr_log(GPR_INFO,
              "[RR %p] Subchannel %p has gone into TRANSIENT_FAILURE. "
              "Requesting re-resolution",
              p, subchannel());
    }
    p->channel_control_helper()->RequestReresolution();
    subchannel()->AttemptToConnect();
  }
  // Update state counters.
  UpdateConnectivityStateLocked(connectivity_state);
  // Update overall state and renew notification.
  subchannel_list()->UpdateRoundRobinStateFromSubchannelStateCountsLocked();
}

void RoundRobin::UpdateLocked(UpdateArgs args) {
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
    // If we already have a subchannel list, then ignore the resolver
    // failure and keep using the existing list.
    if (subchannel_list_ != nullptr) return;
  }
  // Replace latest_pending_subchannel_list_.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_round_robin_trace) &&
      latest_pending_subchannel_list_ != nullptr) {
    gpr_log(GPR_INFO,
            "[RR %p] Shutting down previous pending subchannel list %p", this,
            latest_pending_subchannel_list_.get());
  }
  latest_pending_subchannel_list_ = MakeOrphanable<RoundRobinSubchannelList>(
      this, &grpc_lb_round_robin_trace, std::move(addresses), *args.args);
  if (latest_pending_subchannel_list_->num_subchannels() == 0) {
    // If the new list is empty, immediately promote the new list to the
    // current list and transition to TRANSIENT_FAILURE.
    absl::Status status =
        args.addresses.ok() ? absl::UnavailableError(absl::StrCat(
                                  "empty address list: ", args.resolution_note))
                            : args.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        absl::make_unique<TransientFailurePicker>(status));
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
  } else if (subchannel_list_ == nullptr) {
    // If there is no current list, immediately promote the new list to
    // the current list and start watching it.
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
    subchannel_list_->StartWatchingLocked();
  } else {
    // Start watching the pending list.  It will get swapped into the
    // current list when it reports READY.
    latest_pending_subchannel_list_->StartWatchingLocked();
  }
}

class RoundRobinConfig : public LoadBalancingPolicy::Config {
 public:
  const char* name() const override { return kRoundRobin; }
};

//
// factory
//

class RoundRobinFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<RoundRobin>(std::move(args));
  }

  const char* name() const override { return kRoundRobin; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& /*json*/, grpc_error_handle* /*error*/) const override {
    return MakeRefCounted<RoundRobinConfig>();
  }
};

}  // namespace

}  // namespace grpc_core

void grpc_lb_policy_round_robin_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::RoundRobinFactory>());
}

void grpc_lb_policy_round_robin_shutdown() {}
