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
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/subchannel_list.h"
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
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_pick_first_trace(false, "pick_first");

namespace {

//
// pick_first LB policy
//

constexpr absl::string_view kPickFirst = "pick_first";

class PickFirst : public LoadBalancingPolicy {
 public:
  explicit PickFirst(Args args);

  absl::string_view name() const override { return kPickFirst; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  ~PickFirst() override;

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
        absl::optional<grpc_connectivity_state> old_state,
        grpc_connectivity_state new_state) override;

    // Processes the connectivity change to READY for an unselected subchannel.
    void ProcessUnselectedReadyLocked();
  };

  class PickFirstSubchannelList
      : public SubchannelList<PickFirstSubchannelList,
                              PickFirstSubchannelData> {
   public:
    PickFirstSubchannelList(PickFirst* policy, ServerAddressList addresses,
                            const ChannelArgs& args)
        : SubchannelList(policy,
                         (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)
                              ? "PickFirstSubchannelList"
                              : nullptr),
                         std::move(addresses), policy->channel_control_helper(),
                         args) {
      // Need to maintain a ref to the LB policy as long as we maintain
      // any references to subchannels, since the subchannels'
      // pollset_sets will include the LB policy's pollset_set.
      policy->Ref(DEBUG_LOCATION, "subchannel_list").release();
      // Note that we do not start trying to connect to any subchannel here,
      // since we will wait until we see the initial connectivity state for all
      // subchannels before doing that.
    }

    ~PickFirstSubchannelList() override {
      PickFirst* p = static_cast<PickFirst*>(policy());
      p->Unref(DEBUG_LOCATION, "subchannel_list");
    }

    bool in_transient_failure() const { return in_transient_failure_; }
    void set_in_transient_failure(bool in_transient_failure) {
      in_transient_failure_ = in_transient_failure;
    }

    size_t attempting_index() const { return attempting_index_; }
    void set_attempting_index(size_t index) { attempting_index_ = index; }

   private:
    std::shared_ptr<WorkSerializer> work_serializer() const override {
      return static_cast<PickFirst*>(policy())->work_serializer();
    }

    bool in_transient_failure_ = false;
    size_t attempting_index_ = 0;
  };

  class Picker : public SubchannelPicker {
   public:
    explicit Picker(RefCountedPtr<SubchannelInterface> subchannel)
        : subchannel_(std::move(subchannel)) {}

    PickResult Pick(PickArgs /*args*/) override {
      return PickResult::Complete(subchannel_);
    }

   private:
    RefCountedPtr<SubchannelInterface> subchannel_;
  };

  void ShutdownLocked() override;

  void AttemptToConnectUsingLatestUpdateArgsLocked();

  // Lateset update args.
  UpdateArgs latest_update_args_;
  // All our subchannels.
  RefCountedPtr<PickFirstSubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  RefCountedPtr<PickFirstSubchannelList> latest_pending_subchannel_list_;
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
  // Create a subchannel list from latest_update_args_.
  ServerAddressList addresses;
  if (latest_update_args_.addresses.ok()) {
    addresses = *latest_update_args_.addresses;
  }
  // Replace latest_pending_subchannel_list_.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace) &&
      latest_pending_subchannel_list_ != nullptr) {
    gpr_log(GPR_INFO,
            "[PF %p] Shutting down previous pending subchannel list %p", this,
            latest_pending_subchannel_list_.get());
  }
  latest_pending_subchannel_list_ = MakeRefCounted<PickFirstSubchannelList>(
      this, std::move(addresses), latest_update_args_.args);
  latest_pending_subchannel_list_->StartWatchingLocked();
  // Empty update or no valid subchannels.  Put the channel in
  // TRANSIENT_FAILURE and request re-resolution.
  if (latest_pending_subchannel_list_->num_subchannels() == 0) {
    absl::Status status =
        latest_update_args_.addresses.ok()
            ? absl::UnavailableError(absl::StrCat(
                  "empty address list: ", latest_update_args_.resolution_note))
            : latest_update_args_.addresses.status();
    channel_control_helper()->UpdateState(
        GRPC_CHANNEL_TRANSIENT_FAILURE, status,
        MakeRefCounted<TransientFailurePicker>(status));
    channel_control_helper()->RequestReresolution();
  }
  // If the new update is empty or we don't yet have a selected subchannel in
  // the current list, replace the current subchannel list immediately.
  if (latest_pending_subchannel_list_->num_subchannels() == 0 ||
      selected_ == nullptr) {
    selected_ = nullptr;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace) &&
        subchannel_list_ != nullptr) {
      gpr_log(GPR_INFO, "[PF %p] Shutting down previous subchannel list %p",
              this, subchannel_list_.get());
    }
    subchannel_list_ = std::move(latest_pending_subchannel_list_);
  }
}

absl::Status PickFirst::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    if (args.addresses.ok()) {
      gpr_log(GPR_INFO,
              "Pick First %p received update with %" PRIuPTR " addresses", this,
              args.addresses->size());
    } else {
      gpr_log(GPR_INFO, "Pick First %p received update with address error: %s",
              this, args.addresses.status().ToString().c_str());
    }
  }
  // Add GRPC_ARG_INHIBIT_HEALTH_CHECKING channel arg.
  args.args = args.args.Set(GRPC_ARG_INHIBIT_HEALTH_CHECKING, 1);
  // Set return status based on the address list.
  absl::Status status;
  if (!args.addresses.ok()) {
    status = args.addresses.status();
  } else if (args.addresses->empty()) {
    status = absl::UnavailableError("address list must not be empty");
  }
  // If the update contains a resolver error and we have a previous update
  // that was not a resolver error, keep using the previous addresses.
  if (!args.addresses.ok() && latest_update_args_.config != nullptr) {
    args.addresses = std::move(latest_update_args_.addresses);
  }
  // Update latest_update_args_.
  latest_update_args_ = std::move(args);
  // If we are not in idle, start connection attempt immediately.
  // Otherwise, we defer the attempt into ExitIdleLocked().
  if (!idle_) {
    AttemptToConnectUsingLatestUpdateArgsLocked();
  }
  return status;
}

void PickFirst::PickFirstSubchannelData::ProcessConnectivityChangeLocked(
    absl::optional<grpc_connectivity_state> old_state,
    grpc_connectivity_state new_state) {
  PickFirst* p = static_cast<PickFirst*>(subchannel_list()->policy());
  // The notification must be for a subchannel in either the current or
  // latest pending subchannel lists.
  GPR_ASSERT(subchannel_list() == p->subchannel_list_.get() ||
             subchannel_list() == p->latest_pending_subchannel_list_.get());
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  // Handle updates for the currently selected subchannel.
  if (p->selected_ == this) {
    GPR_ASSERT(subchannel_list() == p->subchannel_list_.get());
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Pick First %p selected subchannel connectivity changed to %s", p,
              ConnectivityStateName(new_state));
    }
    // Any state change is considered to be a failure of the existing
    // connection.
    // If there is a pending update, switch to the pending update.
    if (p->latest_pending_subchannel_list_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_INFO,
                "Pick First %p promoting pending subchannel list %p to "
                "replace %p",
                p, p->latest_pending_subchannel_list_.get(),
                p->subchannel_list_.get());
      }
      p->selected_ = nullptr;
      p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
      // Set our state to that of the pending subchannel list.
      if (p->subchannel_list_->in_transient_failure()) {
        absl::Status status = absl::UnavailableError(absl::StrCat(
            "selected subchannel failed; switching to pending update; "
            "last failure: ",
            p->subchannel_list_
                ->subchannel(p->subchannel_list_->num_subchannels())
                ->connectivity_status()
                .ToString()));
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_TRANSIENT_FAILURE, status,
            MakeRefCounted<TransientFailurePicker>(status));
      } else {
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::Status(),
            MakeRefCounted<QueuePicker>(p->Ref(DEBUG_LOCATION, "QueuePicker")));
      }
      return;
    }
    // If the selected subchannel goes bad, request a re-resolution.
    // TODO(qianchengz): We may want to request re-resolution in
    // ExitIdleLocked().
    p->channel_control_helper()->RequestReresolution();
    // TODO(roth): We chould check the connectivity states of all the
    // subchannels here, just in case one of them happens to be READY,
    // and we could switch to that rather than going IDLE.
    // Enter idle.
    p->idle_ = true;
    p->selected_ = nullptr;
    p->subchannel_list_.reset();
    p->channel_control_helper()->UpdateState(
        GRPC_CHANNEL_IDLE, absl::Status(),
        MakeRefCounted<QueuePicker>(p->Ref(DEBUG_LOCATION, "QueuePicker")));
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
  // If the subchannel is READY, use it.
  if (new_state == GRPC_CHANNEL_READY) {
    subchannel_list()->set_in_transient_failure(false);
    ProcessUnselectedReadyLocked();
    return;
  }
  // If this is the initial connectivity state notification for this
  // subchannel, check to see if it's the last one we were waiting for,
  // in which case we start trying to connect to the first subchannel.
  // Otherwise, do nothing, since we'll continue to wait until all of
  // the subchannels report their state.
  if (!old_state.has_value()) {
    if (subchannel_list()->AllSubchannelsSeenInitialState()) {
      subchannel_list()->subchannel(0)->subchannel()->RequestConnection();
    }
    return;
  }
  // Ignore any other updates for subchannels we're not currently trying to
  // connect to.
  if (Index() != subchannel_list()->attempting_index()) return;
  // Otherwise, process connectivity state.
  switch (new_state) {
    case GRPC_CHANNEL_READY:
      // Already handled this case above, so this should not happen.
      GPR_UNREACHABLE_CODE(break);
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      size_t next_index = (Index() + 1) % subchannel_list()->num_subchannels();
      subchannel_list()->set_attempting_index(next_index);
      PickFirstSubchannelData* sd = subchannel_list()->subchannel(next_index);
      // If we're tried all subchannels, set state to TRANSIENT_FAILURE.
      if (sd->Index() == 0) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
          gpr_log(GPR_INFO,
                  "Pick First %p subchannel list %p failed to connect to "
                  "all subchannels",
                  p, subchannel_list());
        }
        subchannel_list()->set_in_transient_failure(true);
        // In case 2, swap to the new subchannel list.  This means reporting
        // TRANSIENT_FAILURE and dropping the existing (working) connection,
        // but we can't ignore what the control plane has told us.
        if (subchannel_list() == p->latest_pending_subchannel_list_.get()) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
            gpr_log(GPR_INFO,
                    "Pick First %p promoting pending subchannel list %p to "
                    "replace %p",
                    p, p->latest_pending_subchannel_list_.get(),
                    p->subchannel_list_.get());
          }
          p->selected_ = nullptr;  // owned by p->subchannel_list_
          p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
        }
        // If this is the current subchannel list (either because we were
        // in case 1 or because we were in case 2 and just promoted it to
        // be the current list), re-resolve and report new state.
        if (subchannel_list() == p->subchannel_list_.get()) {
          p->channel_control_helper()->RequestReresolution();
          absl::Status status = absl::UnavailableError(
              absl::StrCat("failed to connect to all addresses; last error: ",
                           connectivity_status().ToString()));
          p->channel_control_helper()->UpdateState(
              GRPC_CHANNEL_TRANSIENT_FAILURE, status,
              MakeRefCounted<TransientFailurePicker>(status));
        }
      }
      // If the next subchannel is in IDLE, trigger a connection attempt.
      // If it's in READY, we can't get here, because we would already
      // have selected the subchannel above.
      // If it's already in CONNECTING, we don't need to do this.
      // If it's in TRANSIENT_FAILURE, then we will trigger the
      // connection attempt later when it reports IDLE.
      auto sd_state = sd->connectivity_state();
      if (sd_state.has_value() && *sd_state == GRPC_CHANNEL_IDLE) {
        sd->subchannel()->RequestConnection();
      }
      break;
    }
    case GRPC_CHANNEL_IDLE: {
      subchannel()->RequestConnection();
      break;
    }
    case GRPC_CHANNEL_CONNECTING: {
      // Only update connectivity state in case 1, and only if we're not
      // already in TRANSIENT_FAILURE.
      if (subchannel_list() == p->subchannel_list_.get() &&
          !subchannel_list()->in_transient_failure()) {
        p->channel_control_helper()->UpdateState(
            GRPC_CHANNEL_CONNECTING, absl::Status(),
            MakeRefCounted<QueuePicker>(p->Ref(DEBUG_LOCATION, "QueuePicker")));
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
      GRPC_CHANNEL_READY, absl::Status(),
      MakeRefCounted<Picker>(subchannel()->Ref()));
  for (size_t i = 0; i < subchannel_list()->num_subchannels(); ++i) {
    if (i != Index()) {
      subchannel_list()->subchannel(i)->ShutdownLocked();
    }
  }
}

class PickFirstConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kPickFirst; }
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

  absl::string_view name() const override { return kPickFirst; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& /*json*/) const override {
    return MakeRefCounted<PickFirstConfig>();
  }
};

}  // namespace

void RegisterPickFirstLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<PickFirstFactory>());
}

}  // namespace grpc_core
