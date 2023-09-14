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

#include "src/core/ext/filters/client_channel/lb_policy/pick_first/pick_first.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/health_check_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_lb_pick_first_trace(false, "pick_first");

namespace {

//
// pick_first LB policy
//

constexpr absl::string_view kPickFirst = "pick_first";

class PickFirstConfig : public LoadBalancingPolicy::Config {
 public:
  absl::string_view name() const override { return kPickFirst; }
  bool shuffle_addresses() const { return shuffle_addresses_; }

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto kJsonLoader =
        JsonObjectLoader<PickFirstConfig>()
            .OptionalField("shuffleAddressList",
                           &PickFirstConfig::shuffle_addresses_)
            .Finish();
    return kJsonLoader;
  }

 private:
  bool shuffle_addresses_ = false;
};

class PickFirst : public LoadBalancingPolicy {
 public:
  explicit PickFirst(Args args);

  absl::string_view name() const override { return kPickFirst; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  ~PickFirst() override;

  class SubchannelList : public InternallyRefCounted<SubchannelList> {
   public:
    class SubchannelData {
     public:
      SubchannelData(SubchannelList* subchannel_list,
                     RefCountedPtr<SubchannelInterface> subchannel);

      SubchannelInterface* subchannel() const { return subchannel_.get(); }
      absl::optional<grpc_connectivity_state> connectivity_state() const {
        return connectivity_state_;
      }

      // Returns the index into the subchannel list of this object.
      size_t Index() const {
        return static_cast<size_t>(this -
                                   &subchannel_list_->subchannels_.front());
      }

      // Resets the connection backoff.
      void ResetBackoffLocked() {
        if (subchannel_ != nullptr) subchannel_->ResetBackoff();
      }

      // Cancels any pending connectivity watch and unrefs the subchannel.
      void ShutdownLocked();

     private:
      // Watcher for subchannel connectivity state.
      class Watcher
          : public SubchannelInterface::ConnectivityStateWatcherInterface {
       public:
        Watcher(SubchannelData* subchannel_data,
                RefCountedPtr<SubchannelList> subchannel_list)
            : subchannel_data_(subchannel_data),
              subchannel_list_(std::move(subchannel_list)) {}

        ~Watcher() override {
          subchannel_list_.reset(DEBUG_LOCATION, "Watcher dtor");
        }

        void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                       absl::Status status) override {
          subchannel_data_->OnConnectivityStateChange(new_state,
                                                      std::move(status));
        }

        grpc_pollset_set* interested_parties() override {
          return subchannel_list_->policy_->interested_parties();
        }

       private:
        SubchannelData* subchannel_data_;
        RefCountedPtr<SubchannelList> subchannel_list_;
      };

      // This method will be invoked once soon after instantiation to report
      // the current connectivity state, and it will then be invoked again
      // whenever the connectivity state changes.
      void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                     absl::Status status);

      // Processes the connectivity change to READY for an unselected
      // subchannel.
      void ProcessUnselectedReadyLocked();

      // Reacts to the current connectivity state while trying to connect.
      void ReactToConnectivityStateLocked();

      // Backpointer to owning subchannel list.  Not owned.
      SubchannelList* subchannel_list_;
      // The subchannel.
      RefCountedPtr<SubchannelInterface> subchannel_;
      // Will be non-null when the subchannel's state is being watched.
      SubchannelInterface::ConnectivityStateWatcherInterface* pending_watcher_ =
          nullptr;
      // Data updated by the watcher.
      absl::optional<grpc_connectivity_state> connectivity_state_;
      absl::Status connectivity_status_;
    };

    SubchannelList(RefCountedPtr<PickFirst> policy,
                   EndpointAddressesList addresses, const ChannelArgs& args);

    ~SubchannelList() override;

    // The number of subchannels in the list.
    size_t size() const { return subchannels_.size(); }

    // Resets connection backoff of all subchannels.
    void ResetBackoffLocked();

    void Orphan() override;

   private:
    // Returns true if all subchannels have seen their initial
    // connectivity state notifications.
    bool AllSubchannelsSeenInitialState();

    // Backpointer to owning policy.
    RefCountedPtr<PickFirst> policy_;

    ChannelArgs args_;

    // The list of subchannels.
    std::vector<SubchannelData> subchannels_;

    // Is this list shutting down? This may be true due to the shutdown of the
    // policy itself or because a newer update has arrived while this one hadn't
    // finished processing.
    bool shutting_down_ = false;

    bool in_transient_failure_ = false;
    size_t attempting_index_ = 0;
  };

  class HealthWatcher
      : public SubchannelInterface::ConnectivityStateWatcherInterface {
   public:
    explicit HealthWatcher(RefCountedPtr<PickFirst> policy)
        : policy_(std::move(policy)) {}

    ~HealthWatcher() override {
      policy_.reset(DEBUG_LOCATION, "HealthWatcher dtor");
    }

    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   absl::Status status) override;

    grpc_pollset_set* interested_parties() override {
      return policy_->interested_parties();
    }

   private:
    RefCountedPtr<PickFirst> policy_;
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

  void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                   RefCountedPtr<SubchannelPicker> picker);

  void AttemptToConnectUsingLatestUpdateArgsLocked();

  void UnsetSelectedSubchannel();

  // Whether we should enable health watching.
  const bool enable_health_watch_;
  // Whether we should omit our status message prefix.
  const bool omit_status_message_prefix_;

  // Lateset update args.
  UpdateArgs latest_update_args_;
  // All our subchannels.
  OrphanablePtr<SubchannelList> subchannel_list_;
  // Latest pending subchannel list.
  OrphanablePtr<SubchannelList> latest_pending_subchannel_list_;
  // Selected subchannel in subchannel_list_.
  SubchannelList::SubchannelData* selected_ = nullptr;
  // Health watcher for the selected subchannel.
  SubchannelInterface::ConnectivityStateWatcherInterface* health_watcher_ =
      nullptr;
  SubchannelInterface::DataWatcherInterface* health_data_watcher_ = nullptr;
  // Current connectivity state.
  grpc_connectivity_state state_ = GRPC_CHANNEL_CONNECTING;
  // Are we shut down?
  bool shutdown_ = false;
  // Random bit generator used for shuffling addresses if configured
  absl::BitGen bit_gen_;
};

PickFirst::PickFirst(Args args)
    : LoadBalancingPolicy(std::move(args)),
      enable_health_watch_(
          channel_args()
              .GetBool(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING)
              .value_or(false)),
      omit_status_message_prefix_(
          channel_args()
              .GetBool(GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX)
              .value_or(false)) {
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
  UnsetSelectedSubchannel();
  subchannel_list_.reset();
  latest_pending_subchannel_list_.reset();
}

void PickFirst::ExitIdleLocked() {
  if (shutdown_) return;
  if (state_ == GRPC_CHANNEL_IDLE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO, "Pick First %p exiting idle", this);
    }
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
  EndpointAddressesList addresses;
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
  latest_pending_subchannel_list_ = MakeOrphanable<SubchannelList>(
      Ref(), std::move(addresses), latest_update_args_.args);
  // Empty update or no valid subchannels.  Put the channel in
  // TRANSIENT_FAILURE and request re-resolution.
  if (latest_pending_subchannel_list_->size() == 0) {
    channel_control_helper()->RequestReresolution();
    absl::Status status =
        latest_update_args_.addresses.ok()
            ? absl::UnavailableError(absl::StrCat(
                  "empty address list: ", latest_update_args_.resolution_note))
            : latest_update_args_.addresses.status();
    UpdateState(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                MakeRefCounted<TransientFailurePicker>(status));
  }
  // If the new update is empty or we don't yet have a selected subchannel in
  // the current list, replace the current subchannel list immediately.
  if (latest_pending_subchannel_list_->size() == 0 || selected_ == nullptr) {
    UnsetSelectedSubchannel();
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
  // Set return status based on the address list.
  absl::Status status;
  if (!args.addresses.ok()) {
    status = args.addresses.status();
  } else if (args.addresses->empty()) {
    status = absl::UnavailableError("address list must not be empty");
  } else {
    // Shuffle the list if needed.
    auto config = static_cast<PickFirstConfig*>(args.config.get());
    if (config->shuffle_addresses()) {
      absl::c_shuffle(*args.addresses, bit_gen_);
    }
    // Flatten the list so that we have one address per endpoint.
    EndpointAddressesList endpoints;
    for (const auto& endpoint : *args.addresses) {
      for (const auto& address : endpoint.addresses()) {
        endpoints.emplace_back(address, endpoint.args());
      }
    }
    args.addresses = std::move(endpoints);
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
  if (state_ != GRPC_CHANNEL_IDLE) {
    AttemptToConnectUsingLatestUpdateArgsLocked();
  }
  return status;
}

void PickFirst::UpdateState(grpc_connectivity_state state,
                            const absl::Status& status,
                            RefCountedPtr<SubchannelPicker> picker) {
  state_ = state;
  channel_control_helper()->UpdateState(state, status, std::move(picker));
}

void PickFirst::UnsetSelectedSubchannel() {
  if (selected_ != nullptr && health_data_watcher_ != nullptr) {
    selected_->subchannel()->CancelDataWatcher(health_data_watcher_);
  }
  selected_ = nullptr;
  health_watcher_ = nullptr;
  health_data_watcher_ = nullptr;
}

//
// PickFirst::HealthWatcher
//

void PickFirst::HealthWatcher::OnConnectivityStateChange(
    grpc_connectivity_state new_state, absl::Status status) {
  if (policy_->health_watcher_ != this) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "[PF %p] health watch state update: %s (%s)",
            policy_.get(), ConnectivityStateName(new_state),
            status.ToString().c_str());
  }
  switch (new_state) {
    case GRPC_CHANNEL_READY:
      policy_->channel_control_helper()->UpdateState(
          GRPC_CHANNEL_READY, absl::OkStatus(),
          MakeRefCounted<Picker>(policy_->selected_->subchannel()->Ref()));
      break;
    case GRPC_CHANNEL_IDLE:
      // If the subchannel becomes disconnected, the health watcher
      // might happen to see the change before the raw connectivity
      // state watcher does.  In this case, ignore it, since the raw
      // connectivity state watcher will handle it shortly.
      break;
    case GRPC_CHANNEL_CONNECTING:
      policy_->channel_control_helper()->UpdateState(
          new_state, absl::OkStatus(),
          MakeRefCounted<QueuePicker>(policy_->Ref()));
      break;
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      policy_->channel_control_helper()->UpdateState(
          GRPC_CHANNEL_TRANSIENT_FAILURE, status,
          MakeRefCounted<TransientFailurePicker>(status));
      break;
    case GRPC_CHANNEL_SHUTDOWN:
      Crash("health watcher reported state SHUTDOWN");
  }
}

//
// PickFirst::SubchannelList::SubchannelData
//

PickFirst::SubchannelList::SubchannelData::SubchannelData(
    SubchannelList* subchannel_list,
    RefCountedPtr<SubchannelInterface> subchannel)
    : subchannel_list_(subchannel_list), subchannel_(std::move(subchannel)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO,
            "[PF %p] subchannel list %p index %" PRIuPTR
            " (subchannel %p): starting watch",
            subchannel_list_->policy_.get(), subchannel_list_,
            subchannel_list_->size(), subchannel_.get());
  }
  auto watcher = std::make_unique<Watcher>(
      this, subchannel_list_->Ref(DEBUG_LOCATION, "Watcher"));
  pending_watcher_ = watcher.get();
  subchannel_->WatchConnectivityState(std::move(watcher));
}

void PickFirst::SubchannelList::SubchannelData::ShutdownLocked() {
  if (subchannel_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "[PF %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): cancelling watch and unreffing subchannel",
              subchannel_list_->policy_.get(), subchannel_list_, Index(),
              subchannel_list_->size(), subchannel_.get());
    }
    subchannel_->CancelConnectivityStateWatch(pending_watcher_);
    pending_watcher_ = nullptr;
    subchannel_.reset();
  }
}

void PickFirst::SubchannelList::SubchannelData::OnConnectivityStateChange(
    grpc_connectivity_state new_state, absl::Status status) {
  PickFirst* p = subchannel_list_->policy_.get();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(
        GPR_INFO,
        "[PF %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
        " (subchannel %p): connectivity changed: old_state=%s, new_state=%s, "
        "status=%s, shutting_down=%d, pending_watcher=%p, "
        "p->selected_=%p, p->subchannel_list_=%p, "
        "p->latest_pending_subchannel_list_=%p",
        p, subchannel_list_, Index(), subchannel_list_->size(),
        subchannel_.get(),
        (connectivity_state_.has_value()
             ? ConnectivityStateName(*connectivity_state_)
             : "N/A"),
        ConnectivityStateName(new_state), status.ToString().c_str(),
        subchannel_list_->shutting_down_, pending_watcher_, p->selected_,
        p->subchannel_list_.get(), p->latest_pending_subchannel_list_.get());
  }
  if (subchannel_list_->shutting_down_ || pending_watcher_ == nullptr) return;
  // The notification must be for a subchannel in either the current or
  // latest pending subchannel lists.
  GPR_ASSERT(subchannel_list_ == p->subchannel_list_.get() ||
             subchannel_list_ == p->latest_pending_subchannel_list_.get());
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  absl::optional<grpc_connectivity_state> old_state = connectivity_state_;
  connectivity_state_ = new_state;
  connectivity_status_ = status;
  // Handle updates for the currently selected subchannel.
  if (p->selected_ == this) {
    GPR_ASSERT(subchannel_list_ == p->subchannel_list_.get());
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Pick First %p selected subchannel connectivity changed to %s", p,
              ConnectivityStateName(new_state));
    }
    // Any state change is considered to be a failure of the existing
    // connection.
    // TODO(roth): We could check the connectivity states of all the
    // subchannels here, just in case one of them happens to be READY,
    // and we could switch to that rather than going IDLE.
    // Request a re-resolution.
    // TODO(qianchengz): We may want to request re-resolution in
    // ExitIdleLocked().
    p->channel_control_helper()->RequestReresolution();
    // If there is a pending update, switch to the pending update.
    if (p->latest_pending_subchannel_list_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_INFO,
                "Pick First %p promoting pending subchannel list %p to "
                "replace %p",
                p, p->latest_pending_subchannel_list_.get(),
                p->subchannel_list_.get());
      }
      p->UnsetSelectedSubchannel();
      p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
      // Set our state to that of the pending subchannel list.
      if (p->subchannel_list_->in_transient_failure_) {
        absl::Status status = absl::UnavailableError(absl::StrCat(
            "selected subchannel failed; switching to pending update; "
            "last failure: ",
            p->subchannel_list_->subchannels_.back()
                .connectivity_status_.ToString()));
        p->UpdateState(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                       MakeRefCounted<TransientFailurePicker>(status));
      } else if (p->state_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
        p->UpdateState(GRPC_CHANNEL_CONNECTING, absl::Status(),
                       MakeRefCounted<QueuePicker>(nullptr));
      }
      return;
    }
    // Enter idle.
    p->UnsetSelectedSubchannel();
    p->subchannel_list_.reset();
    p->UpdateState(
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
    subchannel_list_->in_transient_failure_ = false;
    ProcessUnselectedReadyLocked();
    return;
  }
  // If we haven't yet seen the initial connectivity state notification
  // for all subchannels, do nothing.
  if (!subchannel_list_->AllSubchannelsSeenInitialState()) return;
  // If we're still here and this is the initial connectivity state
  // notification for this subchannel, that means it was the last one to
  // see its initial notification.  Start trying to connect, starting
  // with the first subchannel.
  if (!old_state.has_value()) {
    subchannel_list_->subchannels_.front().ReactToConnectivityStateLocked();
    return;
  }
  // Ignore any other updates for subchannels we're not currently trying to
  // connect to.
  if (Index() != subchannel_list_->attempting_index_) return;
  // React to the connectivity state.
  ReactToConnectivityStateLocked();
}

void PickFirst::SubchannelList::SubchannelData::
    ReactToConnectivityStateLocked() {
  PickFirst* p = subchannel_list_->policy_.get();
  // Otherwise, process connectivity state.
  switch (connectivity_state_.value()) {
    case GRPC_CHANNEL_READY:
      // Already handled this case above, so this should not happen.
      GPR_UNREACHABLE_CODE(break);
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      // Find the next subchannel not in state TRANSIENT_FAILURE.
      // We skip subchannels in state TRANSIENT_FAILURE to avoid a
      // large recursion that could overflow the stack.
      SubchannelData* found_subchannel = nullptr;
      for (size_t next_index = Index() + 1;
           next_index < subchannel_list_->size(); ++next_index) {
        SubchannelData* sc = &subchannel_list_->subchannels_[next_index];
        GPR_ASSERT(sc->connectivity_state_.has_value());
        if (sc->connectivity_state_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
          subchannel_list_->attempting_index_ = next_index;
          found_subchannel = sc;
          break;
        }
      }
      // If we found another subchannel in the list not in state
      // TRANSIENT_FAILURE, trigger the right behavior for that subchannel.
      if (found_subchannel != nullptr) {
        found_subchannel->ReactToConnectivityStateLocked();
        break;
      }
      // We didn't find another subchannel not in state TRANSIENT_FAILURE,
      // so report TRANSIENT_FAILURE and wait for the first subchannel
      // in the list to report IDLE before continuing.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_INFO,
                "Pick First %p subchannel list %p failed to connect to "
                "all subchannels",
                p, subchannel_list_);
      }
      subchannel_list_->attempting_index_ = 0;
      subchannel_list_->in_transient_failure_ = true;
      // In case 2, swap to the new subchannel list.  This means reporting
      // TRANSIENT_FAILURE and dropping the existing (working) connection,
      // but we can't ignore what the control plane has told us.
      if (subchannel_list_ == p->latest_pending_subchannel_list_.get()) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
          gpr_log(GPR_INFO,
                  "Pick First %p promoting pending subchannel list %p to "
                  "replace %p",
                  p, p->latest_pending_subchannel_list_.get(),
                  p->subchannel_list_.get());
        }
        p->UnsetSelectedSubchannel();
        p->subchannel_list_ = std::move(p->latest_pending_subchannel_list_);
      }
      // If this is the current subchannel list (either because we were
      // in case 1 or because we were in case 2 and just promoted it to
      // be the current list), re-resolve and report new state.
      if (subchannel_list_ == p->subchannel_list_.get()) {
        p->channel_control_helper()->RequestReresolution();
        absl::Status status = absl::UnavailableError(absl::StrCat(
            (p->omit_status_message_prefix_
                 ? ""
                 : "failed to connect to all addresses; last error: "),
            connectivity_status_.ToString()));
        p->UpdateState(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                       MakeRefCounted<TransientFailurePicker>(status));
      }
      // If the first subchannel is already IDLE, trigger the next connection
      // attempt immediately. Otherwise, we'll wait for it to report
      // its own connectivity state change.
      auto& subchannel0 = subchannel_list_->subchannels_.front();
      if (subchannel0.connectivity_state_ == GRPC_CHANNEL_IDLE) {
        subchannel0.subchannel_->RequestConnection();
      }
      break;
    }
    case GRPC_CHANNEL_IDLE:
      subchannel_->RequestConnection();
      break;
    case GRPC_CHANNEL_CONNECTING:
      // Only update connectivity state in case 1, and only if we're not
      // already in TRANSIENT_FAILURE.
      if (subchannel_list_ == p->subchannel_list_.get() &&
          p->state_ != GRPC_CHANNEL_TRANSIENT_FAILURE) {
        p->UpdateState(GRPC_CHANNEL_CONNECTING, absl::Status(),
                       MakeRefCounted<QueuePicker>(nullptr));
      }
      break;
    case GRPC_CHANNEL_SHUTDOWN:
      GPR_UNREACHABLE_CODE(break);
  }
}

void PickFirst::SubchannelList::SubchannelData::ProcessUnselectedReadyLocked() {
  PickFirst* p = subchannel_list_->policy_.get();
  // If we get here, there are two possible cases:
  // 1. We do not currently have a selected subchannel, and the update is
  //    for a subchannel in p->subchannel_list_ that we're trying to
  //    connect to.  The goal here is to find a subchannel that we can
  //    select.
  // 2. We do currently have a selected subchannel, and the update is
  //    for a subchannel in p->latest_pending_subchannel_list_.  The
  //    goal here is to find a subchannel from the update that we can
  //    select in place of the current one.
  GPR_ASSERT(subchannel_list_ == p->subchannel_list_.get() ||
             subchannel_list_ == p->latest_pending_subchannel_list_.get());
  // Case 2.  Promote p->latest_pending_subchannel_list_ to p->subchannel_list_.
  if (subchannel_list_ == p->latest_pending_subchannel_list_.get()) {
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
    gpr_log(GPR_INFO, "Pick First %p selected subchannel %p", p,
            subchannel_.get());
  }
  p->selected_ = this;
  // If health checking is enabled, start the health watch, but don't
  // report a new picker -- we want to stay in CONNECTING while we wait
  // for the health status notification.
  // If health checking is NOT enabled, report READY.
  if (p->enable_health_watch_) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO, "[PF %p] starting health watch", p);
    }
    auto watcher = std::make_unique<HealthWatcher>(
        p->Ref(DEBUG_LOCATION, "HealthWatcher"));
    p->health_watcher_ = watcher.get();
    auto health_data_watcher = MakeHealthCheckWatcher(
        p->work_serializer(), subchannel_list_->args_, std::move(watcher));
    p->health_data_watcher_ = health_data_watcher.get();
    subchannel_->AddDataWatcher(std::move(health_data_watcher));
  } else {
    p->UpdateState(GRPC_CHANNEL_READY, absl::Status(),
                   MakeRefCounted<Picker>(subchannel()->Ref()));
  }
  // Unref all other subchannels in the list.
  for (size_t i = 0; i < subchannel_list_->size(); ++i) {
    if (i != Index()) {
      subchannel_list_->subchannels_[i].ShutdownLocked();
    }
  }
}

//
// PickFirst::SubchannelList
//

PickFirst::SubchannelList::SubchannelList(RefCountedPtr<PickFirst> policy,
                                          EndpointAddressesList addresses,
                                          const ChannelArgs& args)
    : InternallyRefCounted<SubchannelList>(
          GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace) ? "SubchannelList"
                                                            : nullptr),
      policy_(std::move(policy)),
      args_(args.Remove(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING)
                .Remove(
                    GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO,
            "[PF %p] Creating subchannel list %p for %" PRIuPTR
            " subchannels - channel args: %s",
            policy_.get(), this, addresses.size(), args_.ToString().c_str());
  }
  subchannels_.reserve(addresses.size());
  // Create a subchannel for each address.
  for (const EndpointAddresses& address : addresses) {
    GPR_ASSERT(address.addresses().size() == 1);
    RefCountedPtr<SubchannelInterface> subchannel =
        policy_->channel_control_helper()->CreateSubchannel(
            address.address(), address.args(), args_);
    if (subchannel == nullptr) {
      // Subchannel could not be created.
      if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
        gpr_log(GPR_INFO,
                "[PF %p] could not create subchannel for address %s, ignoring",
                policy_.get(), address.ToString().c_str());
      }
      continue;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "[PF %p] subchannel list %p index %" PRIuPTR
              ": Created subchannel %p for address %s",
              policy_.get(), this, subchannels_.size(), subchannel.get(),
              address.ToString().c_str());
    }
    subchannels_.emplace_back(this, std::move(subchannel));
  }
}

PickFirst::SubchannelList::~SubchannelList() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "[PF %p] Destroying subchannel_list %p", policy_.get(),
            this);
  }
}

void PickFirst::SubchannelList::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "[PF %p] Shutting down subchannel_list %p", policy_.get(),
            this);
  }
  GPR_ASSERT(!shutting_down_);
  shutting_down_ = true;
  for (auto& sd : subchannels_) {
    sd.ShutdownLocked();
  }
  Unref();
}

void PickFirst::SubchannelList::ResetBackoffLocked() {
  for (auto& sd : subchannels_) {
    sd.ResetBackoffLocked();
  }
}

bool PickFirst::SubchannelList::AllSubchannelsSeenInitialState() {
  for (auto& sd : subchannels_) {
    if (!sd.connectivity_state().has_value()) return false;
  }
  return true;
}

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
  ParseLoadBalancingConfig(const Json& json) const override {
    return LoadFromJson<RefCountedPtr<PickFirstConfig>>(
        json, JsonArgs(), "errors validating pick_first LB policy config");
  }
};

}  // namespace

void RegisterPickFirstLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<PickFirstFactory>());
}

}  // namespace grpc_core
