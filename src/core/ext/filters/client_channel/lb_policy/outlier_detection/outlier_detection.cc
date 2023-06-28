//
// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h"

#include <inttypes.h>
#include <stddef.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/lb_policy/health_check_client_internal.h"
#include "src/core/ext/filters/client_channel/subchannel_interface_internal.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/validation_errors.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/delegating_helper.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/lb_policy_factory.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/transport/connectivity_state.h"

namespace grpc_core {

TraceFlag grpc_outlier_detection_lb_trace(false, "outlier_detection_lb");

namespace {

using ::grpc_event_engine::experimental::EventEngine;

constexpr absl::string_view kOutlierDetection =
    "outlier_detection_experimental";

// Config for xDS Cluster Impl LB policy.
class OutlierDetectionLbConfig : public LoadBalancingPolicy::Config {
 public:
  OutlierDetectionLbConfig(
      OutlierDetectionConfig outlier_detection_config,
      RefCountedPtr<LoadBalancingPolicy::Config> child_policy)
      : outlier_detection_config_(outlier_detection_config),
        child_policy_(std::move(child_policy)) {}

  absl::string_view name() const override { return kOutlierDetection; }

  bool CountingEnabled() const {
    return outlier_detection_config_.success_rate_ejection.has_value() ||
           outlier_detection_config_.failure_percentage_ejection.has_value();
  }

  const OutlierDetectionConfig& outlier_detection_config() const {
    return outlier_detection_config_;
  }

  RefCountedPtr<LoadBalancingPolicy::Config> child_policy() const {
    return child_policy_;
  }

 private:
  OutlierDetectionConfig outlier_detection_config_;
  RefCountedPtr<LoadBalancingPolicy::Config> child_policy_;
};

// xDS Cluster Impl LB policy.
class OutlierDetectionLb : public LoadBalancingPolicy {
 public:
  explicit OutlierDetectionLb(Args args);

  absl::string_view name() const override { return kOutlierDetection; }

  absl::Status UpdateLocked(UpdateArgs args) override;
  void ExitIdleLocked() override;
  void ResetBackoffLocked() override;

 private:
  class SubchannelState;
  class SubchannelWrapper : public DelegatingSubchannel {
   public:
    SubchannelWrapper(RefCountedPtr<SubchannelState> subchannel_state,
                      RefCountedPtr<SubchannelInterface> subchannel,
                      bool disable_via_raw_connectivity_watch)
        : DelegatingSubchannel(std::move(subchannel)),
          subchannel_state_(std::move(subchannel_state)),
          disable_via_raw_connectivity_watch_(
              disable_via_raw_connectivity_watch) {
      if (subchannel_state_ != nullptr) {
        subchannel_state_->AddSubchannel(this);
        if (subchannel_state_->ejection_time().has_value()) {
          ejected_ = true;
        }
      }
    }

    ~SubchannelWrapper() override {
      if (subchannel_state_ != nullptr) {
        subchannel_state_->RemoveSubchannel(this);
      }
    }

    void Eject();

    void Uneject();

    void WatchConnectivityState(
        std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override;

    void CancelConnectivityStateWatch(
        ConnectivityStateWatcherInterface* watcher) override;

    void AddDataWatcher(std::unique_ptr<DataWatcherInterface> watcher) override;

    RefCountedPtr<SubchannelState> subchannel_state() const {
      return subchannel_state_;
    }

   private:
    // TODO(roth): As a temporary hack, this needs to handle watchers
    // stored as both unique_ptr<> and shared_ptr<>, since the former is
    // used for raw connectivity state watches and the latter is used
    // for health watches.  This hack will go away as part of implementing
    // dualstack backend support.
    class WatcherWrapper
        : public SubchannelInterface::ConnectivityStateWatcherInterface {
     public:
      WatcherWrapper(std::shared_ptr<
                         SubchannelInterface::ConnectivityStateWatcherInterface>
                         health_watcher,
                     bool ejected)
          : watcher_(std::move(health_watcher)), ejected_(ejected) {}

      WatcherWrapper(std::unique_ptr<
                         SubchannelInterface::ConnectivityStateWatcherInterface>
                         watcher,
                     bool ejected)
          : watcher_(std::move(watcher)), ejected_(ejected) {}

      void Eject() {
        ejected_ = true;
        if (last_seen_state_.has_value()) {
          watcher()->OnConnectivityStateChange(
              GRPC_CHANNEL_TRANSIENT_FAILURE,
              absl::UnavailableError(
                  "subchannel ejected by outlier detection"));
        }
      }

      void Uneject() {
        ejected_ = false;
        if (last_seen_state_.has_value()) {
          watcher()->OnConnectivityStateChange(*last_seen_state_,
                                               last_seen_status_);
        }
      }

      void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                     absl::Status status) override {
        const bool send_update = !last_seen_state_.has_value() || !ejected_;
        last_seen_state_ = new_state;
        last_seen_status_ = status;
        if (send_update) {
          if (ejected_) {
            new_state = GRPC_CHANNEL_TRANSIENT_FAILURE;
            status = absl::UnavailableError(
                "subchannel ejected by outlier detection");
          }
          watcher()->OnConnectivityStateChange(new_state, status);
        }
      }

      grpc_pollset_set* interested_parties() override {
        return watcher()->interested_parties();
      }

     private:
      SubchannelInterface::ConnectivityStateWatcherInterface* watcher() const {
        return Match(
            watcher_,
            [](const std::shared_ptr<
                SubchannelInterface::ConnectivityStateWatcherInterface>&
                   watcher) { return watcher.get(); },
            [](const std::unique_ptr<
                SubchannelInterface::ConnectivityStateWatcherInterface>&
                   watcher) { return watcher.get(); });
      }

      absl::variant<std::shared_ptr<
                        SubchannelInterface::ConnectivityStateWatcherInterface>,
                    std::unique_ptr<
                        SubchannelInterface::ConnectivityStateWatcherInterface>>
          watcher_;
      absl::optional<grpc_connectivity_state> last_seen_state_;
      absl::Status last_seen_status_;
      bool ejected_;
    };

    RefCountedPtr<SubchannelState> subchannel_state_;
    const bool disable_via_raw_connectivity_watch_;
    bool ejected_ = false;
    std::map<SubchannelInterface::ConnectivityStateWatcherInterface*,
             WatcherWrapper*>
        watchers_;
    WatcherWrapper* watcher_wrapper_ = nullptr;  // For health watching.
  };

  class SubchannelState : public RefCounted<SubchannelState> {
   public:
    struct Bucket {
      std::atomic<uint64_t> successes;
      std::atomic<uint64_t> failures;
    };

    void RotateBucket() {
      backup_bucket_->successes = 0;
      backup_bucket_->failures = 0;
      current_bucket_.swap(backup_bucket_);
      active_bucket_.store(current_bucket_.get());
    }

    absl::optional<std::pair<double, uint64_t>> GetSuccessRateAndVolume() {
      uint64_t total_request =
          backup_bucket_->successes + backup_bucket_->failures;
      if (total_request == 0) {
        return absl::nullopt;
      }
      double success_rate =
          backup_bucket_->successes * 100.0 /
          (backup_bucket_->successes + backup_bucket_->failures);
      return {
          {success_rate, backup_bucket_->successes + backup_bucket_->failures}};
    }

    void AddSubchannel(SubchannelWrapper* wrapper) {
      subchannels_.insert(wrapper);
    }

    void RemoveSubchannel(SubchannelWrapper* wrapper) {
      subchannels_.erase(wrapper);
    }

    void AddSuccessCount() { active_bucket_.load()->successes.fetch_add(1); }

    void AddFailureCount() { active_bucket_.load()->failures.fetch_add(1); }

    absl::optional<Timestamp> ejection_time() const { return ejection_time_; }

    void Eject(const Timestamp& time) {
      ejection_time_ = time;
      ++multiplier_;
      // Ejecting the subchannel may cause the child policy to unref the
      // subchannel, so we need to be prepared for the set to be modified
      // while we are iterating.
      for (auto it = subchannels_.begin(); it != subchannels_.end();) {
        SubchannelWrapper* subchannel = *it;
        ++it;
        subchannel->Eject();
      }
    }

    void Uneject() {
      ejection_time_.reset();
      for (auto& subchannel : subchannels_) {
        subchannel->Uneject();
      }
    }

    bool MaybeUneject(uint64_t base_ejection_time_in_millis,
                      uint64_t max_ejection_time_in_millis) {
      if (!ejection_time_.has_value()) {
        if (multiplier_ > 0) {
          --multiplier_;
        }
      } else {
        GPR_ASSERT(ejection_time_.has_value());
        auto change_time = ejection_time_.value() +
                           Duration::Milliseconds(std::min(
                               base_ejection_time_in_millis * multiplier_,
                               std::max(base_ejection_time_in_millis,
                                        max_ejection_time_in_millis)));
        if (change_time < Timestamp::Now()) {
          Uneject();
          return true;
        }
      }
      return false;
    }

    void DisableEjection() {
      Uneject();
      multiplier_ = 0;
    }

   private:
    std::unique_ptr<Bucket> current_bucket_ = std::make_unique<Bucket>();
    std::unique_ptr<Bucket> backup_bucket_ = std::make_unique<Bucket>();
    // The bucket used to update call counts.
    // Points to either current_bucket or active_bucket.
    std::atomic<Bucket*> active_bucket_{current_bucket_.get()};
    uint32_t multiplier_ = 0;
    absl::optional<Timestamp> ejection_time_;
    std::set<SubchannelWrapper*> subchannels_;
  };

  // A picker that wraps the picker from the child to perform outlier detection.
  class Picker : public SubchannelPicker {
   public:
    Picker(OutlierDetectionLb* outlier_detection_lb,
           RefCountedPtr<SubchannelPicker> picker, bool counting_enabled);

    PickResult Pick(PickArgs args) override;

   private:
    class SubchannelCallTracker;
    RefCountedPtr<SubchannelPicker> picker_;
    bool counting_enabled_;
  };

  class Helper
      : public ParentOwningDelegatingChannelControlHelper<OutlierDetectionLb> {
   public:
    explicit Helper(RefCountedPtr<OutlierDetectionLb> outlier_detection_policy)
        : ParentOwningDelegatingChannelControlHelper(
              std::move(outlier_detection_policy)) {}

    RefCountedPtr<SubchannelInterface> CreateSubchannel(
        const grpc_resolved_address& address,
        const ChannelArgs& per_address_args, const ChannelArgs& args) override;
    void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                     RefCountedPtr<SubchannelPicker> picker) override;
  };

  class EjectionTimer : public InternallyRefCounted<EjectionTimer> {
   public:
    EjectionTimer(RefCountedPtr<OutlierDetectionLb> parent,
                  Timestamp start_time);

    void Orphan() override;

    Timestamp StartTime() const { return start_time_; }

   private:
    void OnTimerLocked();

    RefCountedPtr<OutlierDetectionLb> parent_;
    absl::optional<EventEngine::TaskHandle> timer_handle_;
    Timestamp start_time_;
    absl::BitGen bit_gen_;
  };

  ~OutlierDetectionLb() override;

  // Returns the address map key for an address, or the empty string if
  // the address should be ignored.
  static std::string MakeKeyForAddress(const grpc_resolved_address& address);

  void ShutdownLocked() override;

  OrphanablePtr<LoadBalancingPolicy> CreateChildPolicyLocked(
      const ChannelArgs& args);

  void MaybeUpdatePickerLocked();

  // Current config from the resolver.
  RefCountedPtr<OutlierDetectionLbConfig> config_;

  // Internal state.
  bool shutting_down_ = false;

  OrphanablePtr<LoadBalancingPolicy> child_policy_;

  // Latest state and picker reported by the child policy.
  grpc_connectivity_state state_ = GRPC_CHANNEL_IDLE;
  absl::Status status_;
  RefCountedPtr<SubchannelPicker> picker_;
  std::map<std::string, RefCountedPtr<SubchannelState>> subchannel_state_map_;
  OrphanablePtr<EjectionTimer> ejection_timer_;
};

//
// OutlierDetectionLb::SubchannelWrapper
//

void OutlierDetectionLb::SubchannelWrapper::Eject() {
  ejected_ = true;
  // Ejecting the subchannel may cause the child policy to cancel the watch,
  // so we need to be prepared for the map to be modified while we are
  // iterating.
  for (auto it = watchers_.begin(); it != watchers_.end();) {
    WatcherWrapper* watcher = it->second;
    ++it;
    watcher->Eject();
  }
  if (watcher_wrapper_ != nullptr) watcher_wrapper_->Eject();
}

void OutlierDetectionLb::SubchannelWrapper::Uneject() {
  ejected_ = false;
  for (auto& watcher : watchers_) {
    watcher.second->Uneject();
  }
  if (watcher_wrapper_ != nullptr) watcher_wrapper_->Uneject();
}

void OutlierDetectionLb::SubchannelWrapper::WatchConnectivityState(
    std::unique_ptr<ConnectivityStateWatcherInterface> watcher) {
  if (disable_via_raw_connectivity_watch_) {
    wrapped_subchannel()->WatchConnectivityState(std::move(watcher));
    return;
  }
  ConnectivityStateWatcherInterface* watcher_ptr = watcher.get();
  auto watcher_wrapper =
      std::make_unique<WatcherWrapper>(std::move(watcher), ejected_);
  watchers_.emplace(watcher_ptr, watcher_wrapper.get());
  wrapped_subchannel()->WatchConnectivityState(std::move(watcher_wrapper));
}

void OutlierDetectionLb::SubchannelWrapper::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  if (disable_via_raw_connectivity_watch_) {
    wrapped_subchannel()->CancelConnectivityStateWatch(watcher);
    return;
  }
  auto it = watchers_.find(watcher);
  if (it == watchers_.end()) return;
  wrapped_subchannel()->CancelConnectivityStateWatch(it->second);
  watchers_.erase(it);
}

void OutlierDetectionLb::SubchannelWrapper::AddDataWatcher(
    std::unique_ptr<DataWatcherInterface> watcher) {
  auto* w = static_cast<InternalSubchannelDataWatcherInterface*>(watcher.get());
  if (w->type() == HealthProducer::Type()) {
    auto* health_watcher = static_cast<HealthWatcher*>(watcher.get());
    auto watcher_wrapper = std::make_shared<WatcherWrapper>(
        health_watcher->TakeWatcher(), ejected_);
    watcher_wrapper_ = watcher_wrapper.get();
    health_watcher->SetWatcher(std::move(watcher_wrapper));
  }
  DelegatingSubchannel::AddDataWatcher(std::move(watcher));
}

//
// OutlierDetectionLb::Picker::SubchannelCallTracker
//

class OutlierDetectionLb::Picker::SubchannelCallTracker
    : public LoadBalancingPolicy::SubchannelCallTrackerInterface {
 public:
  SubchannelCallTracker(
      std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
          original_subchannel_call_tracker,
      RefCountedPtr<SubchannelState> subchannel_state)
      : original_subchannel_call_tracker_(
            std::move(original_subchannel_call_tracker)),
        subchannel_state_(std::move(subchannel_state)) {}

  ~SubchannelCallTracker() override {
    subchannel_state_.reset(DEBUG_LOCATION, "SubchannelCallTracker");
  }

  void Start() override {
    // This tracker does not care about started calls only finished calls.
    // Delegate if needed.
    if (original_subchannel_call_tracker_ != nullptr) {
      original_subchannel_call_tracker_->Start();
    }
  }

  void Finish(FinishArgs args) override {
    // Delegate if needed.
    if (original_subchannel_call_tracker_ != nullptr) {
      original_subchannel_call_tracker_->Finish(args);
    }
    // Record call completion based on status for outlier detection
    // calculations.
    if (subchannel_state_ != nullptr) {
      if (args.status.ok()) {
        subchannel_state_->AddSuccessCount();
      } else {
        subchannel_state_->AddFailureCount();
      }
    }
  }

 private:
  std::unique_ptr<LoadBalancingPolicy::SubchannelCallTrackerInterface>
      original_subchannel_call_tracker_;
  RefCountedPtr<SubchannelState> subchannel_state_;
};

//
// OutlierDetectionLb::Picker
//

OutlierDetectionLb::Picker::Picker(OutlierDetectionLb* outlier_detection_lb,
                                   RefCountedPtr<SubchannelPicker> picker,
                                   bool counting_enabled)
    : picker_(std::move(picker)), counting_enabled_(counting_enabled) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] constructed new picker %p and counting "
            "is %s",
            outlier_detection_lb, this,
            (counting_enabled ? "enabled" : "disabled"));
  }
}

LoadBalancingPolicy::PickResult OutlierDetectionLb::Picker::Pick(
    LoadBalancingPolicy::PickArgs args) {
  if (picker_ == nullptr) {  // Should never happen.
    return PickResult::Fail(absl::InternalError(
        "outlier_detection picker not given any child picker"));
  }
  // Delegate to child picker
  PickResult result = picker_->Pick(args);
  auto* complete_pick = absl::get_if<PickResult::Complete>(&result.result);
  if (complete_pick != nullptr) {
    // Unwrap subchannel to pass back up the stack.
    auto* subchannel_wrapper =
        static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
    // Inject subchannel call tracker to record call completion as long as
    // not both success_rate_ejection and failure_percentage_ejection are unset.
    if (counting_enabled_) {
      complete_pick->subchannel_call_tracker =
          std::make_unique<SubchannelCallTracker>(
              std::move(complete_pick->subchannel_call_tracker),
              subchannel_wrapper->subchannel_state());
    }
    complete_pick->subchannel = subchannel_wrapper->wrapped_subchannel();
  }
  return result;
}

//
// OutlierDetectionLb
//

OutlierDetectionLb::OutlierDetectionLb(Args args)
    : LoadBalancingPolicy(std::move(args)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] created", this);
  }
}

OutlierDetectionLb::~OutlierDetectionLb() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] destroying outlier_detection LB policy",
            this);
  }
}

std::string OutlierDetectionLb::MakeKeyForAddress(
    const grpc_resolved_address& address) {
  // Use only the address, not the attributes.
  auto addr_str = grpc_sockaddr_to_string(&address, false);
  // If address couldn't be stringified, ignore it.
  if (!addr_str.ok()) return "";
  return std::move(*addr_str);
}

void OutlierDetectionLb::ShutdownLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] shutting down", this);
  }
  ejection_timer_.reset();
  shutting_down_ = true;
  // Remove the child policy's interested_parties pollset_set from the
  // xDS policy.
  if (child_policy_ != nullptr) {
    grpc_pollset_set_del_pollset_set(child_policy_->interested_parties(),
                                     interested_parties());
    child_policy_.reset();
  }
  // Drop our ref to the child's picker, in case it's holding a ref to
  // the child.
  picker_.reset();
}

void OutlierDetectionLb::ExitIdleLocked() {
  if (child_policy_ != nullptr) child_policy_->ExitIdleLocked();
}

void OutlierDetectionLb::ResetBackoffLocked() {
  if (child_policy_ != nullptr) child_policy_->ResetBackoffLocked();
}

absl::Status OutlierDetectionLb::UpdateLocked(UpdateArgs args) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] Received update", this);
  }
  auto old_config = std::move(config_);
  // Update config.
  config_ = std::move(args.config);
  // Update outlier detection timer.
  if (!config_->CountingEnabled()) {
    // No need for timer.  Cancel the current timer, if any.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] counting disabled, cancelling timer",
              this);
    }
    ejection_timer_.reset();
  } else if (ejection_timer_ == nullptr) {
    // No timer running.  Start it now.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO, "[outlier_detection_lb %p] starting timer", this);
    }
    ejection_timer_ = MakeOrphanable<EjectionTimer>(Ref(), Timestamp::Now());
    for (const auto& p : subchannel_state_map_) {
      p.second->RotateBucket();  // Reset call counters.
    }
  } else if (old_config->outlier_detection_config().interval !=
             config_->outlier_detection_config().interval) {
    // Timer interval changed.  Cancel the current timer and start a new one
    // with the same start time.
    // Note that if the new deadline is in the past, the timer will fire
    // immediately.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] interval changed, replacing timer",
              this);
    }
    ejection_timer_ =
        MakeOrphanable<EjectionTimer>(Ref(), ejection_timer_->StartTime());
  }
  // Update subchannel state map.
  if (args.addresses.ok()) {
    std::set<std::string> current_addresses;
    for (const EndpointAddresses& addresses : *args.addresses) {
// FIXME: support multiple addresses
      std::string address_key = MakeKeyForAddress(addresses.address());
      if (address_key.empty()) continue;
      auto& subchannel_state = subchannel_state_map_[address_key];
      if (subchannel_state == nullptr) {
        subchannel_state = MakeRefCounted<SubchannelState>();
        if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
          gpr_log(GPR_INFO,
                  "[outlier_detection_lb %p] adding map entry for %s (%p)",
                  this, address_key.c_str(), subchannel_state.get());
        }
      } else if (!config_->CountingEnabled()) {
        // If counting is not enabled, reset state.
        if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
          gpr_log(GPR_INFO,
                  "[outlier_detection_lb %p] counting disabled; disabling "
                  "ejection for %s (%p)",
                  this, address_key.c_str(), subchannel_state.get());
        }
        subchannel_state->DisableEjection();
      }
      current_addresses.emplace(address_key);
    }
    for (auto it = subchannel_state_map_.begin();
         it != subchannel_state_map_.end();) {
      if (current_addresses.find(it->first) == current_addresses.end()) {
        // remove each map entry for a subchannel address not in the updated
        // address list.
        if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
          gpr_log(GPR_INFO,
                  "[outlier_detection_lb %p] removing map entry for %s (%p)",
                  this, it->first.c_str(), it->second.get());
        }
        it = subchannel_state_map_.erase(it);
      } else {
        ++it;
      }
    }
  }
  // Create child policy if needed.
  if (child_policy_ == nullptr) {
    child_policy_ = CreateChildPolicyLocked(args.args);
  }
  // Update child policy.
  UpdateArgs update_args;
  update_args.addresses = std::move(args.addresses);
  update_args.resolution_note = std::move(args.resolution_note);
  update_args.config = config_->child_policy();
  // Update the policy.
  update_args.args = std::move(args.args);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] Updating child policy handler %p", this,
            child_policy_.get());
  }
  return child_policy_->UpdateLocked(std::move(update_args));
}

void OutlierDetectionLb::MaybeUpdatePickerLocked() {
  if (picker_ != nullptr) {
    auto outlier_detection_picker =
        MakeRefCounted<Picker>(this, picker_, config_->CountingEnabled());
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] updating connectivity: state=%s "
              "status=(%s) picker=%p",
              this, ConnectivityStateName(state_), status_.ToString().c_str(),
              outlier_detection_picker.get());
    }
    channel_control_helper()->UpdateState(state_, status_,
                                          std::move(outlier_detection_picker));
  }
}

OrphanablePtr<LoadBalancingPolicy> OutlierDetectionLb::CreateChildPolicyLocked(
    const ChannelArgs& args) {
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer();
  lb_policy_args.args = args;
  lb_policy_args.channel_control_helper =
      std::make_unique<Helper>(Ref(DEBUG_LOCATION, "Helper"));
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_outlier_detection_lb_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] Created new child policy handler %p",
            this, lb_policy.get());
  }
  // Add our interested_parties pollset_set to that of the newly created
  // child policy. This will make the child policy progress upon activity on
  // this policy, which in turn is tied to the application's call.
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties());
  return lb_policy;
}

//
// OutlierDetectionLb::Helper
//

RefCountedPtr<SubchannelInterface> OutlierDetectionLb::Helper::CreateSubchannel(
    const grpc_resolved_address& address, const ChannelArgs& per_address_args,
    const ChannelArgs& args) {
  if (parent()->shutting_down_) return nullptr;
  // If the address has the DisableOutlierDetectionAttribute attribute,
  // ignore it for raw connectivity state updates.
  // TODO(roth): This is a hack to prevent outlier_detection from
  // working with pick_first, as per discussion in
  // https://github.com/grpc/grpc/issues/32967.  Remove this as part of
  // implementing dualstack backend support.
  const bool disable_via_raw_connectivity_watch =
      per_address_args.GetInt(GRPC_ARG_OUTLIER_DETECTION_DISABLE) == 1;
  RefCountedPtr<SubchannelState> subchannel_state;
  std::string key = MakeKeyForAddress(address);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] using key %s for subchannel, "
            "disable_via_raw_connectivity_watch=%d",
            parent(), key.c_str(), disable_via_raw_connectivity_watch);
  }
  if (!key.empty()) {
    auto it = parent()->subchannel_state_map_.find(key);
    if (it != parent()->subchannel_state_map_.end()) {
      subchannel_state = it->second->Ref();
    }
  }
  auto subchannel = MakeRefCounted<SubchannelWrapper>(
      subchannel_state,
      parent()->channel_control_helper()->CreateSubchannel(
          address, per_address_args, args),
      disable_via_raw_connectivity_watch);
  if (subchannel_state != nullptr) {
    subchannel_state->AddSubchannel(subchannel.get());
  }
  return subchannel;
}

void OutlierDetectionLb::Helper::UpdateState(
    grpc_connectivity_state state, const absl::Status& status,
    RefCountedPtr<SubchannelPicker> picker) {
  if (parent()->shutting_down_) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] child connectivity state update: "
            "state=%s (%s) picker=%p",
            parent(), ConnectivityStateName(state), status.ToString().c_str(),
            picker.get());
  }
  // Save the state and picker.
  parent()->state_ = state;
  parent()->status_ = status;
  parent()->picker_ = std::move(picker);
  // Wrap the picker and return it to the channel.
  parent()->MaybeUpdatePickerLocked();
}

//
// OutlierDetectionLb::EjectionTimer
//

OutlierDetectionLb::EjectionTimer::EjectionTimer(
    RefCountedPtr<OutlierDetectionLb> parent, Timestamp start_time)
    : parent_(std::move(parent)), start_time_(start_time) {
  auto interval = parent_->config_->outlier_detection_config().interval;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] ejection timer will run in %s",
            parent_.get(), interval.ToString().c_str());
  }
  timer_handle_ = parent_->channel_control_helper()->GetEventEngine()->RunAfter(
      interval, [self = Ref(DEBUG_LOCATION, "EjectionTimer")]() mutable {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        auto self_ptr = self.get();
        self_ptr->parent_->work_serializer()->Run(
            [self = std::move(self)]() { self->OnTimerLocked(); },
            DEBUG_LOCATION);
      });
}

void OutlierDetectionLb::EjectionTimer::Orphan() {
  if (timer_handle_.has_value()) {
    parent_->channel_control_helper()->GetEventEngine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  Unref();
}

void OutlierDetectionLb::EjectionTimer::OnTimerLocked() {
  if (!timer_handle_.has_value()) return;
  timer_handle_.reset();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO, "[outlier_detection_lb %p] ejection timer running",
            parent_.get());
  }
  std::map<SubchannelState*, double> success_rate_ejection_candidates;
  std::map<SubchannelState*, double> failure_percentage_ejection_candidates;
  size_t ejected_host_count = 0;
  double success_rate_sum = 0;
  auto time_now = Timestamp::Now();
  auto& config = parent_->config_->outlier_detection_config();
  for (auto& state : parent_->subchannel_state_map_) {
    auto* subchannel_state = state.second.get();
    // For each address, swap the call counter's buckets in that address's
    // map entry.
    subchannel_state->RotateBucket();
    // Gather data to run success rate algorithm or failure percentage
    // algorithm.
    if (subchannel_state->ejection_time().has_value()) {
      ++ejected_host_count;
    }
    absl::optional<std::pair<double, uint64_t>> host_success_rate_and_volume =
        subchannel_state->GetSuccessRateAndVolume();
    if (!host_success_rate_and_volume.has_value()) {
      continue;
    }
    double success_rate = host_success_rate_and_volume->first;
    uint64_t request_volume = host_success_rate_and_volume->second;
    if (config.success_rate_ejection.has_value()) {
      if (request_volume >= config.success_rate_ejection->request_volume) {
        success_rate_ejection_candidates[subchannel_state] = success_rate;
        success_rate_sum += success_rate;
      }
    }
    if (config.failure_percentage_ejection.has_value()) {
      if (request_volume >=
          config.failure_percentage_ejection->request_volume) {
        failure_percentage_ejection_candidates[subchannel_state] = success_rate;
      }
    }
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
    gpr_log(GPR_INFO,
            "[outlier_detection_lb %p] found %" PRIuPTR
            " success rate candidates and %" PRIuPTR
            " failure percentage candidates; ejected_host_count=%" PRIuPTR
            "; success_rate_sum=%.3f",
            parent_.get(), success_rate_ejection_candidates.size(),
            failure_percentage_ejection_candidates.size(), ejected_host_count,
            success_rate_sum);
  }
  // success rate algorithm
  if (!success_rate_ejection_candidates.empty() &&
      success_rate_ejection_candidates.size() >=
          config.success_rate_ejection->minimum_hosts) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] running success rate algorithm: "
              "stdev_factor=%d, enforcement_percentage=%d",
              parent_.get(), config.success_rate_ejection->stdev_factor,
              config.success_rate_ejection->enforcement_percentage);
    }
    // calculate ejection threshold: (mean - stdev *
    // (success_rate_ejection.stdev_factor / 1000))
    double mean = success_rate_sum / success_rate_ejection_candidates.size();
    double variance = 0;
    for (const auto& p : success_rate_ejection_candidates) {
      variance += std::pow(p.second - mean, 2);
    }
    variance /= success_rate_ejection_candidates.size();
    double stdev = std::sqrt(variance);
    const double success_rate_stdev_factor =
        static_cast<double>(config.success_rate_ejection->stdev_factor) / 1000;
    double ejection_threshold = mean - stdev * success_rate_stdev_factor;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] stdev=%.3f, ejection_threshold=%.3f",
              parent_.get(), stdev, ejection_threshold);
    }
    for (auto& candidate : success_rate_ejection_candidates) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
        gpr_log(GPR_INFO,
                "[outlier_detection_lb %p] checking candidate %p: "
                "success_rate=%.3f",
                parent_.get(), candidate.first, candidate.second);
      }
      if (candidate.second < ejection_threshold) {
        uint32_t random_key = absl::Uniform(bit_gen_, 1, 100);
        double current_percent =
            100.0 * ejected_host_count / parent_->subchannel_state_map_.size();
        if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
          gpr_log(GPR_INFO,
                  "[outlier_detection_lb %p] random_key=%d "
                  "ejected_host_count=%" PRIuPTR " current_percent=%.3f",
                  parent_.get(), random_key, ejected_host_count,
                  current_percent);
        }
        if (random_key < config.success_rate_ejection->enforcement_percentage &&
            (ejected_host_count == 0 ||
             (current_percent < config.max_ejection_percent))) {
          // Eject and record the timestamp for use when ejecting addresses in
          // this iteration.
          if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
            gpr_log(GPR_INFO, "[outlier_detection_lb %p] ejecting candidate",
                    parent_.get());
          }
          candidate.first->Eject(time_now);
          ++ejected_host_count;
        }
      }
    }
  }
  // failure percentage algorithm
  if (!failure_percentage_ejection_candidates.empty() &&
      failure_percentage_ejection_candidates.size() >=
          config.failure_percentage_ejection->minimum_hosts) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO,
              "[outlier_detection_lb %p] running failure percentage algorithm: "
              "threshold=%d, enforcement_percentage=%d",
              parent_.get(), config.failure_percentage_ejection->threshold,
              config.failure_percentage_ejection->enforcement_percentage);
    }
    for (auto& candidate : failure_percentage_ejection_candidates) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
        gpr_log(GPR_INFO,
                "[outlier_detection_lb %p] checking candidate %p: "
                "success_rate=%.3f",
                parent_.get(), candidate.first, candidate.second);
      }
      // Extra check to make sure success rate algorithm didn't already
      // eject this backend.
      if (candidate.first->ejection_time().has_value()) continue;
      if ((100.0 - candidate.second) >
          config.failure_percentage_ejection->threshold) {
        uint32_t random_key = absl::Uniform(bit_gen_, 1, 100);
        double current_percent =
            100.0 * ejected_host_count / parent_->subchannel_state_map_.size();
        if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
          gpr_log(GPR_INFO,
                  "[outlier_detection_lb %p] random_key=%d "
                  "ejected_host_count=%" PRIuPTR " current_percent=%.3f",
                  parent_.get(), random_key, ejected_host_count,
                  current_percent);
        }
        if (random_key <
                config.failure_percentage_ejection->enforcement_percentage &&
            (ejected_host_count == 0 ||
             (current_percent < config.max_ejection_percent))) {
          // Eject and record the timestamp for use when ejecting addresses in
          // this iteration.
          if (GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
            gpr_log(GPR_INFO, "[outlier_detection_lb %p] ejecting candidate",
                    parent_.get());
          }
          candidate.first->Eject(time_now);
          ++ejected_host_count;
        }
      }
    }
  }
  // For each address in the map:
  //   If the address is not ejected and the multiplier is greater than 0,
  //   decrease the multiplier by 1. If the address is ejected, and the
  //   current time is after ejection_timestamp + min(base_ejection_time *
  //   multiplier, max(base_ejection_time, max_ejection_time)), un-eject the
  //   address.
  for (auto& state : parent_->subchannel_state_map_) {
    auto* subchannel_state = state.second.get();
    const bool unejected = subchannel_state->MaybeUneject(
        config.base_ejection_time.millis(), config.max_ejection_time.millis());
    if (unejected && GRPC_TRACE_FLAG_ENABLED(grpc_outlier_detection_lb_trace)) {
      gpr_log(GPR_INFO, "[outlier_detection_lb %p] unejected address %s (%p)",
              parent_.get(), state.first.c_str(), subchannel_state);
    }
  }
  parent_->ejection_timer_ =
      MakeOrphanable<EjectionTimer>(parent_, Timestamp::Now());
}

//
// factory
//

class OutlierDetectionLbFactory : public LoadBalancingPolicyFactory {
 public:
  OrphanablePtr<LoadBalancingPolicy> CreateLoadBalancingPolicy(
      LoadBalancingPolicy::Args args) const override {
    return MakeOrphanable<OutlierDetectionLb>(std::move(args));
  }

  absl::string_view name() const override { return kOutlierDetection; }

  absl::StatusOr<RefCountedPtr<LoadBalancingPolicy::Config>>
  ParseLoadBalancingConfig(const Json& json) const override {
    ValidationErrors errors;
    OutlierDetectionConfig outlier_detection_config;
    RefCountedPtr<LoadBalancingPolicy::Config> child_policy;
    {
      outlier_detection_config =
          LoadFromJson<OutlierDetectionConfig>(json, JsonArgs(), &errors);
      // Parse childPolicy manually.
      {
        ValidationErrors::ScopedField field(&errors, ".childPolicy");
        auto it = json.object().find("childPolicy");
        if (it == json.object().end()) {
          errors.AddError("field not present");
        } else {
          auto child_policy_config = CoreConfiguration::Get()
                                         .lb_policy_registry()
                                         .ParseLoadBalancingConfig(it->second);
          if (!child_policy_config.ok()) {
            errors.AddError(child_policy_config.status().message());
          } else {
            child_policy = std::move(*child_policy_config);
          }
        }
      }
    }
    if (!errors.ok()) {
      return errors.status(
          absl::StatusCode::kInvalidArgument,
          "errors validating outlier_detection LB policy config");
    }
    return MakeRefCounted<OutlierDetectionLbConfig>(outlier_detection_config,
                                                    std::move(child_policy));
  }
};

}  // namespace

//
// OutlierDetectionConfig
//

const JsonLoaderInterface*
OutlierDetectionConfig::SuccessRateEjection::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<SuccessRateEjection>()
          .OptionalField("stdevFactor", &SuccessRateEjection::stdev_factor)
          .OptionalField("enforcementPercentage",
                         &SuccessRateEjection::enforcement_percentage)
          .OptionalField("minimumHosts", &SuccessRateEjection::minimum_hosts)
          .OptionalField("requestVolume", &SuccessRateEjection::request_volume)
          .Finish();
  return loader;
}

void OutlierDetectionConfig::SuccessRateEjection::JsonPostLoad(
    const Json&, const JsonArgs&, ValidationErrors* errors) {
  if (enforcement_percentage > 100) {
    ValidationErrors::ScopedField field(errors, ".enforcement_percentage");
    errors->AddError("value must be <= 100");
  }
}

const JsonLoaderInterface*
OutlierDetectionConfig::FailurePercentageEjection::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<FailurePercentageEjection>()
          .OptionalField("threshold", &FailurePercentageEjection::threshold)
          .OptionalField("enforcementPercentage",
                         &FailurePercentageEjection::enforcement_percentage)
          .OptionalField("minimumHosts",
                         &FailurePercentageEjection::minimum_hosts)
          .OptionalField("requestVolume",
                         &FailurePercentageEjection::request_volume)
          .Finish();
  return loader;
}

void OutlierDetectionConfig::FailurePercentageEjection::JsonPostLoad(
    const Json&, const JsonArgs&, ValidationErrors* errors) {
  if (enforcement_percentage > 100) {
    ValidationErrors::ScopedField field(errors, ".enforcement_percentage");
    errors->AddError("value must be <= 100");
  }
  if (threshold > 100) {
    ValidationErrors::ScopedField field(errors, ".threshold");
    errors->AddError("value must be <= 100");
  }
}

const JsonLoaderInterface* OutlierDetectionConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<OutlierDetectionConfig>()
          .OptionalField("interval", &OutlierDetectionConfig::interval)
          .OptionalField("baseEjectionTime",
                         &OutlierDetectionConfig::base_ejection_time)
          .OptionalField("maxEjectionTime",
                         &OutlierDetectionConfig::max_ejection_time)
          .OptionalField("maxEjectionPercent",
                         &OutlierDetectionConfig::max_ejection_percent)
          .OptionalField("successRateEjection",
                         &OutlierDetectionConfig::success_rate_ejection)
          .OptionalField("failurePercentageEjection",
                         &OutlierDetectionConfig::failure_percentage_ejection)
          .Finish();
  return loader;
}

void OutlierDetectionConfig::JsonPostLoad(const Json& json, const JsonArgs&,
                                          ValidationErrors* errors) {
  if (json.object().find("maxEjectionTime") == json.object().end()) {
    max_ejection_time = std::max(base_ejection_time, Duration::Seconds(300));
  }
  if (max_ejection_percent > 100) {
    ValidationErrors::ScopedField field(errors, ".max_ejection_percent");
    errors->AddError("value must be <= 100");
  }
}

//
// Plugin registration
//

void RegisterOutlierDetectionLbPolicy(CoreConfiguration::Builder* builder) {
  builder->lb_policy_registry()->RegisterLoadBalancingPolicyFactory(
      std::make_unique<OutlierDetectionLbFactory>());
}

}  // namespace grpc_core
