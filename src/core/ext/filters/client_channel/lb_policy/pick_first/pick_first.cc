/*
 *
 * Copyright 2015 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_pick_first_trace(false, "pick_first");

namespace {

//
// pick_first LB policy
//

constexpr char kPickFirst[] = "pick_first";

class PickFirst : public LoadBalancingPolicy {
 public:
  explicit PickFirst(Args args);

  const char* name() const override { return kPickFirst; }

  void UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  ~PickFirst();

  class PickFirstSubchannelList;

  class PickFirstSubchannelData
      : public SubchannelData<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelData(
        SubchannelList<PickFirstSubchannelList, PickFirstSubchannelData>*
            subchannel_list,
        const ServerAddress& address,
        RefCountedPtr<SubchannelInterface> subchannel)
        : SubchannelData(subchannel_list, address, std::move(subchannel)) {}

    void ProcessConnectivityChangeLocked(
        grpc_connectivity_state connectivity_state) override;

    // Processes the connectivity change to READY for an unselected subchannel.
    void ProcessUnselectedReadyLocked();

    void CheckConnectivityStateAndStartWatchingLocked();
  };

  class PickFirstSubchannelList
      : public SubchannelList<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelList(PickFirst* policy, TraceFlag* tracer,
                            const ServerAddressList& addresses,
                            const grpc_channel_args& args)
        : SubchannelList(policy, tracer, addresses,
                         policy->channel_control_helper(), args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
    }

    ~PickFirstSubchannelList() {
      PickFirst* p = static_cast<PickFirst*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    bool in_transient_failure() const { return in_transient_failure_; }
    void set_in_transient_failure(bool in_transient_failure) {
      in_transient_failure_ = in_transient_failure;
    }

   private:
    bool in_transient_failure_ = false;
  };

  class Picker : public SubchannelPicker {
   public:
    explicit Picker(RefCountedPtr<SubchannelInterface> subchannel)
        : subchannel_(std::move(subchannel)) {}

    PickResult Pick(PickArgs /*args*/) override {
      PickResult result;
      result.type = PickResult::PICK_COMPLETE;
      result.subchannel = subchannel_;
      return result;
    }

   private:
    RefCountedPtr<SubchannelInterface> subchannel_;
  };

  void ShutdownLocked() override;

  void AttemptToConnectUsingLatestUpdateArgsLocked();

  // Lateset update args.
  UpdateArgs latest_update_args_;
  // All our subchannels.
  OrphanablePtr<PickFirstSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  OrphanablePtr<PickFirstSubchannelList> latest_pending_subchannel_list_;
  // Selected subchannel in \a subchannel_list_.
  PickFirstSubchannelData* selected_ = nullptr;
  // Are we in IDLE state?
  bool idle_ = false;
  // Are we shut down?
  bool shutdown_ = false;
};

PickFirst::PickFirst(Args args) : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "Pick First %p created.", this);
  }
}

PickFirst::~PickFirst() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "Destroying Pick First %p", this);
  }
  GPR_ASSERT(subchannel_list_ == nullptr);
  GPR_ASSERT(latest_pending_subchannel_list_ == nullptr);
}

void PickFirst::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "Pick First %p Shutting down", this);
  }
  shutdown_ = true;
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
}

void PickFirst::ExitIdleLocked() {
  if (shutdown_) return;
  if (idle_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO, "Pick First %p exiting idle", this);
    }
    idle_ = false;
    AttemptToConnectUsingLatestUpdateArgsLocked();
  }
}

void PickFirst::ResetBackoffLocked() {
  if (subchannel_list_ != nullptr) subchannel_list_->ResetBackoffLocked();
  if (latest_pending_subchannel_list_ != nullptr) {
    latest_pending_subchannel_list_->ResetBackoffLocked();
  }
}

void PickFirst::AttemptToConnectUsingLatestUpdateArgsLocked() {
  // Create a subchannel list from the latest_update_args_.
  auto subchannel_list = MakeOrphanable<PickFirstSubchannelList>(
      this, &grpc_lb_pick_first_trace, latest_update_args_.addresses,
      *latest_update_args_.args);
  // Empty update or no valid subchannels.
  if (subchannel_list->num_subchannels() == 0) {
    // Unsubscribe from all current subchannels.
    subchannel_list_ = std::move(subchannel_list);  // Empty list.
    selected_ = nullptr;
    // If not idle, put the channel in TRANSIENT_FAILURE.
    // (If we are idle, then this will happen in ExitIdleLocked() if we
    // haven't gotten a non-empty update by the time the application tries
    // to start a new call.)
    grpc_error* error =
        grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Empty update"),
                           GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE,
        absl::make_unique<TransientFailurePicker>(error));
    return;
  }
  // If one of the subchannels in the new list is already in state
  // READY, then select it immediately.  This can happen when the
  // currently selected subchannel is also present in the update.  It
  // can also happen if one of the subchannels in the update is already
  // in the global subchannel pool because it's in use by another channel.
  for (size_t i = 0; i < subchannel_list->num_subchannels(); ++i) {
    PickFirstSubchannelData* sd = subchannel_list->subchannel(i);
    grpc_connectivity_state state = sd->CheckConnectivityStateLocked();
    if (state == GRPC_CHANNEL_READY) {
      subchannel_list_ = std::move(subchannel_list);
      sd->StartConnectivityWatchLocked();
      sd->ProcessUnselectedReadyLocked();
      // If there was a previously pending update (which may or may
      // not have contained the currently selected subchannel), drop
      // it, so that it doesn't override what we've done here.
      latest_pending_subchannel_list_.reset();
      return;
    }
  }
  if (selected_ == nullptr) {
    // We don't yet have a selected subchannel, so replace the current
    // subchannel list immediately.
    subchannel_list_ = std::move(subchannel_list);
    // If we're not in IDLE state, start trying to connect to the first
    // subchannel in the new list.
    // Note: No need to use CheckConnectivityStateAndStartWatchingLocked()
    // here, since we've already checked the initial connectivity
    // state of all subchannels above.
    subchannel_list_->subchannel(0)->StartConnectivityWatchLocked();
    subchannel_list_->subchannel(0)->subchannel()->AttemptToConnect();
  } else {
    // We do have a selected subchannel (which means it's READY), so keep
    // using it until one of the subchannels in the new list reports READY.
    if (latest_pending_subchannel_list_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_INFO,
                "Pick First %p Shutting down latest pending subchannel list "
                "%p, about to be replaced by newer latest %p",
                this, latest_pending_subchannel_list_.get(),
                subchannel_list.get());
      }
    }
    latest_pending_subchannel_list_ = std::move(subchannel_list);
    // If we're not in IDLE state, start trying to connect to the first
    // subchannel in the new list.
    // Note: No need to use CheckConnectivityStateAndStartWatchingLocked()
    // here, since we've already checked the initial connectivity
    // state of all subchannels above.
    latest_pending_subchannel_list_->subchannel(0)
        ->StartConnectivityWatchLocked();
    latest_pending_subchannel_list_->subchannel(0)
        ->subchannel()
        ->AttemptToConnect();
  }
}

void PickFirst::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO,
            "Pick First %p received update with %" PRIuPTR " addresses", this,
            args.addresses.size());
  }
  // Update the latest_update_args_
  grpc_arg new_arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_INHIBIT_HEALTH_CHECKING), 1);
  const grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add(args.args, &new_arg, 1);
  GPR_SWAP(const grpc_channel_args*, new_args, args.args);
  grpc_channel_args_destroy(new_args);
  latest_update_args_ = std::move(args);
  // If we are not in idle, start connection attempt immediately.
  // Otherwise, we defer the attempt into ExitIdleLocked().
  if (!idle_) {
    AttemptToConnectUsingLatestUpdateArgsLocked();
  }
}

void PickFirst::PickFirstSubchannelData::ProcessConnectivityChangeLocked(
    grpc_connectivity_state connectivity_state) {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  // The notification must be for a subchannel in either the current or
  // latest pending subchannel lists.
  GPR_ASSERT(subchannel_list() == p->subchannel_list_.get() ||
             subchannel_list() == p->latest_pending_subchannel_list_.get());
  GPR_ASSERT(connectivity_state != GRPC_CHANNEL_SHUTDOWN);
  // Handle updates for the currently selected subchannel.
  if (p->selected_ == this) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Pick First %p selected subchannel connectivity changed to %s", p,
              ConnectivityStateName(connectivity_state));
    }
    // If the new state is anything other than READY and there is a
    // pending update, switch to the pending update.
    if (connectivity_state != GRPC_CHANNEL_READY &&
        p->latest_pending_subchannel_list_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_INFO,
                "Pick First %p promoting pending subchannel list %p to "
                "replace %p",
                p, p->latest_pending_subchannel_list_.get(),
                p->subchannel_list_.get());
      }
      p->selected_ = nullptr;
      CancelConnectivityWatchLocked(
          "selected subchannel failed; switching to pending update");
      p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
      // Set our state to that of the pending subchannel list.
      if (p->subchannel_list_->in_transient_failure()) {
        grpc_error* error = grpc_error_set_int(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "selected subchannel failed; switching to pending update"),
            GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_TRANSIENT_FAILURE,
            absl::make_unique<TransientFailurePicker>(error));
      } else {
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::make_unique<QueuePicker>(p->Ref(
                                         DEBUG_LOCATION, "QueuePicker")));
      }
    } else {
      if (connectivity_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
        // If the selected subchannel goes bad, request a re-resolution. We
        // also set the channel state to IDLE. The reason is that if the new
        // state is TRANSIENT_FAILURE due to a GOAWAY reception we don't want
        // to connect to the re-resolved backends until we leave IDLE state.
        // TODO(qianchengz): We may want to request re-resolution in
        // ExitIdleLocked().
        p->idle_ = true;
        p->channel_control_helper()->RequestReresolution();
        p->selected_ = nullptr;
        p->subchannel_list_.reset();
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_IDLE, absl::make_unique<QueuePicker>(
                                   p->Ref(DEBUG_LOCATION, "QueuePicker")));
      } else {
        // This is unlikely but can happen when a subchannel has been asked
        // to reconnect by a different channel and this channel has dropped
        // some connectivity state notifications.
        if (connectivity_state == GRPC_CHANNEL_READY) {
          p->channel_control_helper()->UpdateState(
              GRPC_CHANNEL_READY,
              absl::make_unique<Picker>(subchannel()->Ref()));
        } else {  // CONNECTING
          p->channel_control_helper()->UpdateState(
              connectivity_state, absl::make_unique<QueuePicker>(
                                      p->Ref(DEBUG_LOCATION, "QueuePicker")));
        }
      }
    }
    return;
  }
  // If we get here, there are two possible cases:
  // 1. We do not currently have a selected subchannel, and the update is
  //    for a subchannel in p->subchannel_list_ that we're trying to
  //    connect to.  The goal here is to find a subchannel that we can
  //    select.
  // 2. We do currently have a selected subchannel, and the update is
  //    for a subchannel in p->latest_pending_subchannel_list_.  The
  //    goal here is to find a subchannel from the update that we can
  //    select in place of the current one.
  subchannel_list()->set_in_transient_failure(false);
  switch (connectivity_state) {
    case GRPC_CHANNEL_READY: {
      ProcessUnselectedReadyLocked();
      break;
    }
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      CancelConnectivityWatchLocked("connection attempt failed");
      PickFirstSubchannelData* sd = this;
      size_t next_index =
          (sd->Index() + 1) % subchannel_list()->num_subchannels();
      sd = subchannel_list()->subchannel(next_index);
      // If we're tried all subchannels, set state to TRANSIENT_FAILURE.
      if (sd->Index() == 0) {
        // Re-resolve if this is the most recent subchannel list.
        if (subchannel_list() == (p->latest_pending_subchannel_list_ != nullptr
                                      ? p->latest_pending_subchannel_list_.get()
                                      : p->subchannel_list_.get())) {
          p->channel_control_helper()->RequestReresolution();
        }
        subchannel_list()->set_in_transient_failure(true);
        // Only report new state in case 1.
        if (subchannel_list() == p->subchannel_list_.get()) {
          grpc_error* error = grpc_error_set_int(
              GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                  "failed to connect to all addresses"),
              GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
          p->channel_control_helper()->UpdateState(
              GRPC_CHANNEL_TRANSIENT_FAILURE,
              absl::make_unique<TransientFailurePicker>(error));
        }
      }
      sd->CheckConnectivityStateAndStartWatchingLocked();
      break;
    }
    case GRPC_CHANNEL_CONNECTING:
    case GRPC_CHANNEL_IDLE: {
      // Only update connectivity state in case 1.
      if (subchannel_list() == p->subchannel_list_.get()) {
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::make_unique<QueuePicker>(p->Ref(
                                         DEBUG_LOCATION, "QueuePicker")));
      }
      break;
    }
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_UNREACHABLE_CODE(break);
  }
}

void PickFirst::PickFirstSubchannelData::ProcessUnselectedReadyLocked() {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  // If we get here, there are two possible cases:
  // 1. We do not currently have a selected subchannel, and the update is
  //    for a subchannel in p->subchannel_list_ that we're trying to
  //    connect to.  The goal here is to find a subchannel that we can
  //    select.
  // 2. We do currently have a selected subchannel, and the update is
  //    for a subchannel in p->latest_pending_subchannel_list_.  The
  //    goal here is to find a subchannel from the update that we can
  //    select in place of the current one.
  GPR_ASSERT(subchannel_list() == p->subchannel_list_.get() ||
             subchannel_list() == p->latest_pending_subchannel_list_.get());
  // Case 2.  Promote p->latest_pending_subchannel_list_ to p->subchannel_list_.
  if (subchannel_list() == p->latest_pending_subchannel_list_.get()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Pick First %p promoting pending subchannel list %p to "
              "replace %p",
              p, p->latest_pending_subchannel_list_.get(),
              p->subchannel_list_.get());
    }
    p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
  }
  // Cases 1 and 2.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "Pick First %p selected subchannel %p", p, subchannel());
  }
  p->selected_ = this;
  p->channel_control_helper()->UpdateState(
      GRPC_CHANNEL_READY, absl::make_unique<Picker>(subchannel()->Ref()));
  for (size_t i = 0; i < subchannel_list()->num_subchannels(); ++i) {
    if (i != Index()) {
      subchannel_list()->subchannel(i)->ShutdownLocked();
    }
  }
}

void PickFirst::PickFirstSubchannelData::
    CheckConnectivityStateAndStartWatchingLocked() {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  // Check current state.
  grpc_connectivity_state current_state = CheckConnectivityStateLocked();
  // Start watch.
  StartConnectivityWatchLocked();
  // If current state is READY, select the subchannel now, since we started
  // watching from this state and will not get a notification of it
  // transitioning into this state.
  // If the current state is not READY, attempt to connect.
  if (current_state == GRPC_CHANNEL_READY) {
    if (p->selected_ != this) ProcessUnselectedReadyLocked();
  } else {
    subchannel()->AttemptToConnect();
  }
}

class PickFirstConfig : public LoadBalancingPolicy::Config {
 public:
  const char* name() const override { return kPickFirst; }
};

//
// factory
//

class PickFirstFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<PickFirst>(std::move(args));
  }

  const char* name() const override { return kPickFirst; }

  RefCountedPtr<LoadBalancingPolicy::Config> ParseLoadBalancingConfig(
      const Json& json, grpc_error** /*error*/) const override {
    return MakeRefCounted<PickFirstConfig>();
  }
};

}  // namespace

}  // namespace grpc_core

void grpc_lb_policy_pick_first_init() {
  grpc_core::LoadBalancingPolicyRegistry::Builder::
      RegisterLoadBalancingPolicyFactory(
          absl::make_unique<grpc_core::PickFirstFactory>());
}

void grpc_lb_policy_pick_first_shutdown() {}
