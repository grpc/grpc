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

#include "src/core/load_balancing/pick_first/pick_first.h"

#include <inttypes.h>
#include <string.h>

#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/load_balancing/health_check_client.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_factory.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/resolver/endpoint_addresses.h"

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
      SubchannelData(SubchannelList* subchannel_list, size_t index,
                     RefCountedPtr<SubchannelInterface> subchannel);

      SubchannelInterface* subchannel() const { return subchannel_.get(); }
      absl::optional<grpc_connectivity_state> connectivity_state() const {
        return connectivity_state_;
      }
      const absl::Status& connectivity_status() const {
        return connectivity_status_;
      }

      // Resets the connection backoff.
      void ResetBackoffLocked() {
        if (subchannel_ != nullptr) subchannel_->ResetBackoff();
      }

      void RequestConnection() { subchannel_->RequestConnection(); }

      // Requests a connection attempt to start on this subchannel,
      // with appropriate Connection Attempt Delay.
      // Used only during the Happy Eyeballs pass.
      void RequestConnectionWithTimer();

      // Cancels any pending connectivity watch and unrefs the subchannel.
      void ShutdownLocked();

      bool seen_transient_failure() const { return seen_transient_failure_; }

     private:
      // Watcher for subchannel connectivity state.
      class Watcher
          : public SubchannelInterface::ConnectivityStateWatcherInterface {
       public:
        Watcher(RefCountedPtr<SubchannelList> subchannel_list, size_t index)
            : subchannel_list_(std::move(subchannel_list)), index_(index) {}

        ~Watcher() override {
          subchannel_list_.reset(DEBUG_LOCATION, "Watcher dtor");
        }

        void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                       absl::Status status) override {
          subchannel_list_->subchannels_[index_].OnConnectivityStateChange(
              new_state, std::move(status));
        }

        grpc_pollset_set* interested_parties() override {
          return subchannel_list_->policy_->interested_parties();
        }

       private:
        RefCountedPtr<SubchannelList> subchannel_list_;
        const size_t index_;
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
      // TODO(roth): Remove this when we remove the Happy Eyeballs experiment.
      void ReactToConnectivityStateLocked();

      // Backpointer to owning subchannel list.  Not owned.
      SubchannelList* subchannel_list_;
      const size_t index_;
      // The subchannel.
      RefCountedPtr<SubchannelInterface> subchannel_;
      // Will be non-null when the subchannel's state is being watched.
      SubchannelInterface::ConnectivityStateWatcherInterface* pending_watcher_ =
          nullptr;
      // Data updated by the watcher.
      absl::optional<grpc_connectivity_state> connectivity_state_;
      absl::Status connectivity_status_;
      bool seen_transient_failure_ = false;
    };

    SubchannelList(RefCountedPtr<PickFirst> policy,
                   EndpointAddressesIterator* addresses,
                   const ChannelArgs& args);

    ~SubchannelList() override;

    // The number of subchannels in the list.
    size_t size() const { return subchannels_.size(); }

    // Resets connection backoff of all subchannels.
    void ResetBackoffLocked();

    void Orphan() override;

    bool IsHappyEyeballsPassComplete() const {
      // Checking attempting_index_ here is just an optimization -- if
      // we haven't actually tried all subchannels yet, then we don't
      // need to iterate.
      if (attempting_index_ < size()) return false;
      for (const SubchannelData& sd : subchannels_) {
        if (!sd.seen_transient_failure()) return false;
      }
      return true;
    }

   private:
    // Returns true if all subchannels have seen their initial
    // connectivity state notifications.
    bool AllSubchannelsSeenInitialState() const {
      return num_subchannels_seen_initial_notification_ == size();
    }

    // Looks through subchannels_ starting from attempting_index_ to
    // find the first one not currently in TRANSIENT_FAILURE, then
    // triggers a connection attempt for that subchannel.  If there are
    // no more subchannels not in TRANSIENT_FAILURE, calls
    // MaybeFinishHappyEyeballsPass().
    void StartConnectingNextSubchannel();

    // Checks to see if the initial Happy Eyeballs pass is complete --
    // i.e., all subchannels have seen TRANSIENT_FAILURE state at least once.
    // If so, transitions to a mode where we try to connect to all subchannels
    // in parallel and returns true.
    void MaybeFinishHappyEyeballsPass();

    // Backpointer to owning policy.
    RefCountedPtr<PickFirst> policy_;

    ChannelArgs args_;

    // The list of subchannels.
    std::vector<SubchannelData> subchannels_;

    // Is this list shutting down? This may be true due to the shutdown of the
    // policy itself or because a newer update has arrived while this one hadn't
    // finished processing.
    bool shutting_down_ = false;

    // TODO(roth): Remove this when we remove the Happy Eyeballs experiment.
    bool in_transient_failure_ = false;

    size_t num_subchannels_seen_initial_notification_ = 0;

    // The index into subchannels_ to which we are currently attempting
    // to connect during the initial Happy Eyeballs pass.  Once the
    // initial pass is over, this will be equal to size().
    size_t attempting_index_ = 0;
    // Happy Eyeballs timer handle.
    absl::optional<grpc_event_engine::experimental::EventEngine::TaskHandle>
        timer_handle_;

    // After the initial Happy Eyeballs pass, the number of failures
    // we've seen.  Every size() failures, we trigger re-resolution.
    size_t num_failures_ = 0;

    // The status from the last subchannel that reported TRANSIENT_FAILURE.
    absl::Status last_failure_;
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

  // When ExitIdleLocked() is called, we create a subchannel_list_ and start
  // trying to connect, but we don't actually change state_ until the first
  // subchannel reports CONNECTING.  So in order to know if we're really
  // idle, we need to check both state_ and subchannel_list_.
  bool IsIdle() const {
    return state_ == GRPC_CHANNEL_IDLE && subchannel_list_ == nullptr;
  }

  // Whether we should enable health watching.
  const bool enable_health_watch_;
  // Whether we should omit our status message prefix.
  const bool omit_status_message_prefix_;
  // Connection Attempt Delay for Happy Eyeballs.
  const Duration connection_attempt_delay_;

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
              .value_or(false)),
      connection_attempt_delay_(Duration::Milliseconds(
          Clamp(channel_args()
                    .GetInt(GRPC_ARG_HAPPY_EYEBALLS_CONNECTION_ATTEMPT_DELAY_MS)
                    .value_or(250),
                100, 2000))) {
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
  if (IsIdle()) {
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
  EndpointAddressesIterator* addresses = nullptr;
  if (latest_update_args_.addresses.ok()) {
    addresses = latest_update_args_.addresses->get();
  }
  // Replace latest_pending_subchannel_list_.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace) &&
      latest_pending_subchannel_list_ != nullptr) {
    gpr_log(GPR_INFO,
            "[PF %p] Shutting down previous pending subchannel list %p", this,
            latest_pending_subchannel_list_.get());
  }
  latest_pending_subchannel_list_ = MakeOrphanable<SubchannelList>(
      RefAsSubclass<PickFirst>(), addresses, latest_update_args_.args);
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

absl::string_view GetAddressFamily(const grpc_resolved_address& address) {
  const char* uri_scheme = grpc_sockaddr_get_uri_scheme(&address);
  return absl::string_view(uri_scheme == nullptr ? "other" : uri_scheme);
};

// An endpoint list iterator that returns only entries for a specific
// address family, as indicated by the URI scheme.
class AddressFamilyIterator {
 public:
  AddressFamilyIterator(absl::string_view scheme, size_t index)
      : scheme_(scheme), index_(index) {}

  EndpointAddresses* Next(EndpointAddressesList& endpoints,
                          std::vector<bool>* endpoints_moved) {
    for (; index_ < endpoints.size(); ++index_) {
      if (!(*endpoints_moved)[index_] &&
          GetAddressFamily(endpoints[index_].address()) == scheme_) {
        (*endpoints_moved)[index_] = true;
        return &endpoints[index_++];
      }
    }
    return nullptr;
  }

 private:
  absl::string_view scheme_;
  size_t index_;
};

absl::Status PickFirst::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    if (args.addresses.ok()) {
      gpr_log(GPR_INFO, "Pick First %p received update", this);
    } else {
      gpr_log(GPR_INFO, "Pick First %p received update with address error: %s",
              this, args.addresses.status().ToString().c_str());
    }
  }
  // Set return status based on the address list.
  absl::Status status;
  if (!args.addresses.ok()) {
    status = args.addresses.status();
  } else {
    EndpointAddressesList endpoints;
    (*args.addresses)->ForEach([&](const EndpointAddresses& endpoint) {
      endpoints.push_back(endpoint);
    });
    if (endpoints.empty()) {
      status = absl::UnavailableError("address list must not be empty");
    } else {
      // Shuffle the list if needed.
      auto config = static_cast<PickFirstConfig*>(args.config.get());
      if (config->shuffle_addresses()) {
        absl::c_shuffle(endpoints, bit_gen_);
      }
      // Flatten the list so that we have one address per endpoint.
      // While we're iterating, also determine the desired address family
      // order and the index of the first element of each family, for use in
      // the interleaving below.
      std::set<absl::string_view> address_families;
      std::vector<AddressFamilyIterator> address_family_order;
      EndpointAddressesList flattened_endpoints;
      for (const auto& endpoint : endpoints) {
        for (const auto& address : endpoint.addresses()) {
          flattened_endpoints.emplace_back(address, endpoint.args());
          if (IsPickFirstHappyEyeballsEnabled()) {
            absl::string_view scheme = GetAddressFamily(address);
            bool inserted = address_families.insert(scheme).second;
            if (inserted) {
              address_family_order.emplace_back(scheme,
                                                flattened_endpoints.size() - 1);
            }
          }
        }
      }
      endpoints = std::move(flattened_endpoints);
      // Interleave addresses as per RFC-8305 section 4.
      if (IsPickFirstHappyEyeballsEnabled()) {
        EndpointAddressesList interleaved_endpoints;
        interleaved_endpoints.reserve(endpoints.size());
        std::vector<bool> endpoints_moved(endpoints.size());
        size_t scheme_index = 0;
        for (size_t i = 0; i < endpoints.size(); ++i) {
          EndpointAddresses* endpoint;
          do {
            auto& iterator = address_family_order[scheme_index++ %
                                                  address_family_order.size()];
            endpoint = iterator.Next(endpoints, &endpoints_moved);
          } while (endpoint == nullptr);
          interleaved_endpoints.emplace_back(std::move(*endpoint));
        }
        endpoints = std::move(interleaved_endpoints);
      }
      args.addresses =
          std::make_shared<EndpointAddressesListIterator>(std::move(endpoints));
    }
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
  if (!IsIdle()) {
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
    SubchannelList* subchannel_list, size_t index,
    RefCountedPtr<SubchannelInterface> subchannel)
    : subchannel_list_(subchannel_list),
      index_(index),
      subchannel_(std::move(subchannel)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO,
            "[PF %p] subchannel list %p index %" PRIuPTR
            " (subchannel %p): starting watch",
            subchannel_list_->policy_.get(), subchannel_list_, index_,
            subchannel_.get());
  }
  auto watcher = std::make_unique<Watcher>(
      subchannel_list_->Ref(DEBUG_LOCATION, "Watcher"), index_);
  pending_watcher_ = watcher.get();
  subchannel_->WatchConnectivityState(std::move(watcher));
}

void PickFirst::SubchannelList::SubchannelData::ShutdownLocked() {
  if (subchannel_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "[PF %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): cancelling watch and unreffing subchannel",
              subchannel_list_->policy_.get(), subchannel_list_, index_,
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
        "seen_transient_failure=%d, p->selected_=%p, "
        "p->subchannel_list_=%p, p->latest_pending_subchannel_list_=%p",
        p, subchannel_list_, index_, subchannel_list_->size(),
        subchannel_.get(),
        (connectivity_state_.has_value()
             ? ConnectivityStateName(*connectivity_state_)
             : "N/A"),
        ConnectivityStateName(new_state), status.ToString().c_str(),
        subchannel_list_->shutting_down_, pending_watcher_,
        seen_transient_failure_, p->selected_, p->subchannel_list_.get(),
        p->latest_pending_subchannel_list_.get());
  }
  if (subchannel_list_->shutting_down_ || pending_watcher_ == nullptr) return;
  // The notification must be for a subchannel in either the current or
  // latest pending subchannel lists.
  GPR_ASSERT(subchannel_list_ == p->subchannel_list_.get() ||
             subchannel_list_ == p->latest_pending_subchannel_list_.get());
  GPR_ASSERT(new_state != GRPC_CHANNEL_SHUTDOWN);
  absl::optional<grpc_connectivity_state> old_state = connectivity_state_;
  connectivity_state_ = new_state;
  connectivity_status_ = std::move(status);
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
      if (IsPickFirstHappyEyeballsEnabled()
              ? p->subchannel_list_->IsHappyEyeballsPassComplete()
              : p->subchannel_list_->in_transient_failure_) {
        status = absl::UnavailableError(absl::StrCat(
            "selected subchannel failed; switching to pending update; "
            "last failure: ",
            p->subchannel_list_->last_failure_.ToString()));
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
    if (!IsPickFirstHappyEyeballsEnabled()) {
      subchannel_list_->in_transient_failure_ = false;
    }
    ProcessUnselectedReadyLocked();
    return;
  }
  // Make sure we note when a subchannel has seen TRANSIENT_FAILURE.
  bool prev_seen_transient_failure = seen_transient_failure_;
  if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    seen_transient_failure_ = true;
    subchannel_list_->last_failure_ = connectivity_status_;
  }
  // If this is the initial connectivity state update for this subchannel,
  // increment the counter in the subchannel list.
  if (!old_state.has_value()) {
    ++subchannel_list_->num_subchannels_seen_initial_notification_;
  }
  // If we haven't yet seen the initial connectivity state notification
  // for all subchannels, do nothing.
  if (!subchannel_list_->AllSubchannelsSeenInitialState()) return;
  // If we're still here and this is the initial connectivity state
  // notification for this subchannel, that means it was the last one to
  // see its initial notification.  Start trying to connect, starting
  // with the first subchannel.
  if (!old_state.has_value()) {
    if (!IsPickFirstHappyEyeballsEnabled()) {
      subchannel_list_->subchannels_.front().ReactToConnectivityStateLocked();
      return;
    }
    subchannel_list_->StartConnectingNextSubchannel();
    return;
  }
  if (!IsPickFirstHappyEyeballsEnabled()) {
    // Ignore any other updates for subchannels we're not currently trying to
    // connect to.
    if (index_ != subchannel_list_->attempting_index_) return;
    // React to the connectivity state.
    ReactToConnectivityStateLocked();
    return;
  }
  // Otherwise, process connectivity state change.
  switch (*connectivity_state_) {
    case GRPC_CHANNEL_TRANSIENT_FAILURE: {
      // If this is the first failure we've seen on this subchannel,
      // then we're still in the Happy Eyeballs pass.
      if (!prev_seen_transient_failure && seen_transient_failure_) {
        // If a connection attempt fails before the timer fires, then
        // cancel the timer and start connecting on the next subchannel.
        if (index_ == subchannel_list_->attempting_index_) {
          if (subchannel_list_->timer_handle_.has_value()) {
            p->channel_control_helper()->GetEventEngine()->Cancel(
                *subchannel_list_->timer_handle_);
          }
          ++subchannel_list_->attempting_index_;
          subchannel_list_->StartConnectingNextSubchannel();
        } else {
          // If this was the last subchannel to fail, check if the Happy
          // Eyeballs pass is complete.
          subchannel_list_->MaybeFinishHappyEyeballsPass();
        }
      } else if (subchannel_list_->IsHappyEyeballsPassComplete()) {
        // We're done with the initial Happy Eyeballs pass and in a mode
        // where we're attempting to connect to every subchannel in
        // parallel.  We count the number of failed connection attempts,
        // and when that is equal to the number of subchannels, request
        // re-resolution and report TRANSIENT_FAILURE again, so that the
        // caller has the most recent status message.  Note that this
        // isn't necessarily the same as saying that we've seen one
        // failure for each subchannel in the list, because the backoff
        // state may be different in each subchannel, so we may have seen
        // one subchannel fail more than once and another subchannel not
        // fail at all.  But it's a good enough heuristic.
        ++subchannel_list_->num_failures_;
        if (subchannel_list_->num_failures_ % subchannel_list_->size() == 0) {
          p->channel_control_helper()->RequestReresolution();
          status = absl::UnavailableError(absl::StrCat(
              (p->omit_status_message_prefix_
                   ? ""
                   : "failed to connect to all addresses; last error: "),
              connectivity_status_.ToString()));
          p->UpdateState(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                         MakeRefCounted<TransientFailurePicker>(status));
        }
      }
      break;
    }
    case GRPC_CHANNEL_IDLE:
      // If we've finished the first Happy Eyeballs pass, then we go
      // into a mode where we immediately try to connect to every
      // subchannel in parallel.
      if (subchannel_list_->IsHappyEyeballsPassComplete()) {
        subchannel_->RequestConnection();
      }
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
    default:
      // We handled READY above, and we should never see SHUTDOWN.
      GPR_UNREACHABLE_CODE(break);
  }
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
      for (size_t next_index = index_ + 1;
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

void PickFirst::SubchannelList::SubchannelData::RequestConnectionWithTimer() {
  GPR_ASSERT(connectivity_state_.has_value());
  if (connectivity_state_ == GRPC_CHANNEL_IDLE) {
    subchannel_->RequestConnection();
  } else {
    GPR_ASSERT(connectivity_state_ == GRPC_CHANNEL_CONNECTING);
  }
  // If this is not the last subchannel in the list, start the timer.
  if (index_ != subchannel_list_->size() - 1) {
    PickFirst* p = subchannel_list_->policy_.get();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Pick First %p subchannel list %p: starting Connection "
              "Attempt Delay timer for %" PRId64 "ms for index %" PRIuPTR,
              p, subchannel_list_, p->connection_attempt_delay_.millis(),
              index_);
    }
    subchannel_list_->timer_handle_ =
        p->channel_control_helper()->GetEventEngine()->RunAfter(
            p->connection_attempt_delay_,
            [subchannel_list =
                 subchannel_list_->Ref(DEBUG_LOCATION, "timer")]() mutable {
              ApplicationCallbackExecCtx application_exec_ctx;
              ExecCtx exec_ctx;
              auto* sl = subchannel_list.get();
              sl->policy_->work_serializer()->Run(
                  [subchannel_list = std::move(subchannel_list)]() {
                    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
                      gpr_log(GPR_INFO,
                              "Pick First %p subchannel list %p: Connection "
                              "Attempt Delay timer fired (shutting_down=%d, "
                              "selected=%p)",
                              subchannel_list->policy_.get(),
                              subchannel_list.get(),
                              subchannel_list->shutting_down_,
                              subchannel_list->policy_->selected_);
                    }
                    if (subchannel_list->shutting_down_) return;
                    if (subchannel_list->policy_->selected_ != nullptr) return;
                    ++subchannel_list->attempting_index_;
                    subchannel_list->StartConnectingNextSubchannel();
                  },
                  DEBUG_LOCATION);
            });
  }
}

void PickFirst::SubchannelList::SubchannelData::ProcessUnselectedReadyLocked() {
  PickFirst* p = subchannel_list_->policy_.get();
  // Cancel Happy Eyeballs timer, if any.
  if (subchannel_list_->timer_handle_.has_value()) {
    p->channel_control_helper()->GetEventEngine()->Cancel(
        *subchannel_list_->timer_handle_);
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
        p->RefAsSubclass<PickFirst>(DEBUG_LOCATION, "HealthWatcher"));
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
    if (i != index_) {
      subchannel_list_->subchannels_[i].ShutdownLocked();
    }
  }
}

//
// PickFirst::SubchannelList
//

PickFirst::SubchannelList::SubchannelList(RefCountedPtr<PickFirst> policy,
                                          EndpointAddressesIterator* addresses,
                                          const ChannelArgs& args)
    : InternallyRefCounted<SubchannelList>(
          GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace) ? "SubchannelList"
                                                            : nullptr),
      policy_(std::move(policy)),
      args_(args.Remove(GRPC_ARG_INTERNAL_PICK_FIRST_ENABLE_HEALTH_CHECKING)
                .Remove(
                    GRPC_ARG_INTERNAL_PICK_FIRST_OMIT_STATUS_MESSAGE_PREFIX)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO, "[PF %p] Creating subchannel list %p - channel args: %s",
            policy_.get(), this, args_.ToString().c_str());
  }
  if (addresses == nullptr) return;
  // Create a subchannel for each address.
  addresses->ForEach([&](const EndpointAddresses& address) {
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
      return;
    }
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "[PF %p] subchannel list %p index %" PRIuPTR
              ": Created subchannel %p for address %s",
              policy_.get(), this, subchannels_.size(), subchannel.get(),
              address.ToString().c_str());
    }
    subchannels_.emplace_back(this, subchannels_.size(), std::move(subchannel));
  });
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
  if (timer_handle_.has_value()) {
    policy_->channel_control_helper()->GetEventEngine()->Cancel(*timer_handle_);
  }
  Unref();
}

void PickFirst::SubchannelList::ResetBackoffLocked() {
  for (auto& sd : subchannels_) {
    sd.ResetBackoffLocked();
  }
}

void PickFirst::SubchannelList::StartConnectingNextSubchannel() {
  // Find the next subchannel not in state TRANSIENT_FAILURE.
  // We skip subchannels in state TRANSIENT_FAILURE to avoid a
  // large recursion that could overflow the stack.
  for (; attempting_index_ < size(); ++attempting_index_) {
    SubchannelData* sc = &subchannels_[attempting_index_];
    GPR_ASSERT(sc->connectivity_state().has_value());
    if (sc->connectivity_state() != GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // Found a subchannel not in TRANSIENT_FAILURE, so trigger a
      // connection attempt.
      sc->RequestConnectionWithTimer();
      return;
    }
  }
  // If we didn't find a subchannel to request a connection on, check to
  // see if the Happy Eyeballs pass is complete.
  MaybeFinishHappyEyeballsPass();
}

void PickFirst::SubchannelList::MaybeFinishHappyEyeballsPass() {
  // Make sure all subchannels have finished a connection attempt before
  // we consider the Happy Eyeballs pass complete.
  if (!IsHappyEyeballsPassComplete()) return;
  // We didn't find another subchannel not in state TRANSIENT_FAILURE,
  // so report TRANSIENT_FAILURE and switch to a mode in which we try to
  // connect to all addresses in parallel.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
    gpr_log(GPR_INFO,
            "Pick First %p subchannel list %p failed to connect to "
            "all subchannels",
            policy_.get(), this);
  }
  // In case 2, swap to the new subchannel list.  This means reporting
  // TRANSIENT_FAILURE and dropping the existing (working) connection,
  // but we can't ignore what the control plane has told us.
  if (policy_->latest_pending_subchannel_list_.get() == this) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_lb_pick_first_trace)) {
      gpr_log(GPR_INFO,
              "Pick First %p promoting pending subchannel list %p to "
              "replace %p",
              policy_.get(), policy_->latest_pending_subchannel_list_.get(),
              this);
    }
    policy_->UnsetSelectedSubchannel();
    policy_->subchannel_list_ =
        std::move(policy_->latest_pending_subchannel_list_);
  }
  // If this is the current subchannel list (either because we were
  // in case 1 or because we were in case 2 and just promoted it to
  // be the current list), re-resolve and report new state.
  if (policy_->subchannel_list_.get() == this) {
    policy_->channel_control_helper()->RequestReresolution();
    absl::Status status = absl::UnavailableError(
        absl::StrCat((policy_->omit_status_message_prefix_
                          ? ""
                          : "failed to connect to all addresses; last error: "),
                     last_failure_.ToString()));
    policy_->UpdateState(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                         MakeRefCounted<TransientFailurePicker>(status));
  }
  // We now transition into a mode where we try to connect to all
  // subchannels in parallel.  For any subchannel currently in IDLE,
  // trigger a connection attempt.  For any subchannel not currently in
  // IDLE, we will trigger a connection attempt when it does report IDLE.
  for (SubchannelData& sd : subchannels_) {
    if (sd.connectivity_state() == GRPC_CHANNEL_IDLE) {
      sd.RequestConnection();
    }
  }
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
