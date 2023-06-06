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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/impl/connectivity_state.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/lb_policy/health_check_client.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/load_balancing/lb_policy.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

// Code for maintaining a list of subchannels within an LB policy.
//
// To use this, callers must create their own subclasses, like so:
//

// class MySubchannelList;  // Forward declaration.

// class MySubchannelData
//   : public SubchannelData<MySubchannelList, MySubchannelData> {
// public:
// void ProcessConnectivityChangeLocked(
//     absl::optional<grpc_connectivity_state> old_state,
//     grpc_connectivity_state new_state) override {
//   // ...code to handle connectivity changes...
// }
// };

// class MySubchannelList
//   : public SubchannelList<MySubchannelList, MySubchannelData> {
// };

//
// All methods will be called from within the client_channel work serializer.

namespace grpc_core {

// Forward declaration.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelList;

// Stores data for a particular subchannel in a subchannel list.
// Callers must create a subclass that implements the
// ProcessConnectivityChangeLocked() method.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelData {
 public:
  // Returns a pointer to the subchannel list containing this object.
  SubchannelListType* subchannel_list() const {
    return static_cast<SubchannelListType*>(subchannel_list_);
  }

  // Returns the index into the subchannel list of this object.
  size_t Index() const {
    return static_cast<size_t>(static_cast<const SubchannelDataType*>(this) -
                               subchannel_list_->subchannel(0));
  }

  // Returns a pointer to the subchannel.
  SubchannelInterface* subchannel() const { return subchannel_.get(); }

  // Returns the cached connectivity state, if any.
  absl::optional<grpc_connectivity_state> connectivity_state() {
    return connectivity_state_;
  }
  absl::Status connectivity_status() { return connectivity_status_; }

  // Resets the connection backoff.
  void ResetBackoffLocked();

  // Cancels any pending connectivity watch and unrefs the subchannel.
  void ShutdownLocked();

 protected:
  SubchannelData(
      SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
      const ServerAddress& address,
      RefCountedPtr<SubchannelInterface> subchannel);

  virtual ~SubchannelData();

  // This method will be invoked once soon after instantiation to report
  // the current connectivity state, and it will then be invoked again
  // whenever the connectivity state changes.
  virtual void ProcessConnectivityChangeLocked(
      absl::optional<grpc_connectivity_state> old_state,
      grpc_connectivity_state new_state) = 0;

 private:
  // For accessing StartConnectivityWatchLocked().
  friend class SubchannelList<SubchannelListType, SubchannelDataType>;

  // Watcher for subchannel connectivity state.
  class Watcher
      : public SubchannelInterface::ConnectivityStateWatcherInterface {
   public:
    Watcher(
        SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data,
        WeakRefCountedPtr<SubchannelListType> subchannel_list)
        : subchannel_data_(subchannel_data),
          subchannel_list_(std::move(subchannel_list)) {}

    ~Watcher() override {
      subchannel_list_.reset(DEBUG_LOCATION, "Watcher dtor");
    }

    void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                   absl::Status status) override;

    grpc_pollset_set* interested_parties() override {
      return subchannel_list_->policy()->interested_parties();
    }

   private:
    SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data_;
    WeakRefCountedPtr<SubchannelListType> subchannel_list_;
  };

  // Starts watching the connectivity state of the subchannel.
  // ProcessConnectivityChangeLocked() will be called whenever the
  // connectivity state changes.
  void StartConnectivityWatchLocked(const ChannelArgs& args);

  // Cancels watching the connectivity state of the subchannel.
  void CancelConnectivityWatchLocked(const char* reason);

  // Unrefs the subchannel.
  void UnrefSubchannelLocked(const char* reason);

  // Backpointer to owning subchannel list.  Not owned.
  SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list_;
  // The subchannel.
  RefCountedPtr<SubchannelInterface> subchannel_;
  // Will be non-null when the subchannel's state is being watched.
  SubchannelInterface::ConnectivityStateWatcherInterface* pending_watcher_ =
      nullptr;
  // Data updated by the watcher.
  absl::optional<grpc_connectivity_state> connectivity_state_;
  absl::Status connectivity_status_;
};

// A list of subchannels.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelList : public DualRefCounted<SubchannelListType> {
 public:
  // Starts watching the connectivity state of all subchannels.
  // Must be called immediately after instantiation.
  void StartWatchingLocked(const ChannelArgs& args);

  // The number of subchannels in the list.
  size_t num_subchannels() const { return subchannels_.size(); }

  // The data for the subchannel at a particular index.
  SubchannelDataType* subchannel(size_t index) {
    return subchannels_[index].get();
  }

  // Returns true if the subchannel list is shutting down.
  bool shutting_down() const { return shutting_down_; }

  // Accessors.
  LoadBalancingPolicy* policy() const { return policy_; }
  const char* tracer() const { return tracer_; }

  // Resets connection backoff of all subchannels.
  void ResetBackoffLocked();

  // Returns true if all subchannels have seen their initial
  // connectivity state notifications.
  bool AllSubchannelsSeenInitialState();

  void Orphan() override;

 protected:
  SubchannelList(LoadBalancingPolicy* policy, const char* tracer,
                 ServerAddressList addresses,
                 LoadBalancingPolicy::ChannelControlHelper* helper,
                 const ChannelArgs& args);

  virtual ~SubchannelList();

 private:
  // For accessing Ref() and Unref().
  friend class SubchannelData<SubchannelListType, SubchannelDataType>;

  virtual std::shared_ptr<WorkSerializer> work_serializer() const = 0;

  // Backpointer to owning policy.
  LoadBalancingPolicy* policy_;

  const char* tracer_;

  absl::optional<std::string> health_check_service_name_;

  // The list of subchannels.
  // We use ManualConstructor here to support SubchannelDataType classes
  // that are not copyable.
  std::vector<ManualConstructor<SubchannelDataType>> subchannels_;

  // Is this list shutting down? This may be true due to the shutdown of the
  // policy itself or because a newer update has arrived while this one hadn't
  // finished processing.
  bool shutting_down_ = false;
};

//
// implementation -- no user-servicable parts below
//

//
// SubchannelData::Watcher
//

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::Watcher::
    OnConnectivityStateChange(grpc_connectivity_state new_state,
                              absl::Status status) {
  if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
    gpr_log(
        GPR_INFO,
        "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
        " (subchannel %p): connectivity changed: old_state=%s, new_state=%s, "
        "status=%s, shutting_down=%d, pending_watcher=%p",
        subchannel_list_->tracer(), subchannel_list_->policy(),
        subchannel_list_.get(), subchannel_data_->Index(),
        subchannel_list_->num_subchannels(),
        subchannel_data_->subchannel_.get(),
        (subchannel_data_->connectivity_state_.has_value()
             ? ConnectivityStateName(*subchannel_data_->connectivity_state_)
             : "N/A"),
        ConnectivityStateName(new_state), status.ToString().c_str(),
        subchannel_list_->shutting_down(), subchannel_data_->pending_watcher_);
  }
  if (!subchannel_list_->shutting_down() &&
      subchannel_data_->pending_watcher_ != nullptr) {
    absl::optional<grpc_connectivity_state> old_state =
        subchannel_data_->connectivity_state_;
    subchannel_data_->connectivity_state_ = new_state;
    subchannel_data_->connectivity_status_ = status;
    // Call the subclass's ProcessConnectivityChangeLocked() method.
    subchannel_data_->ProcessConnectivityChangeLocked(old_state, new_state);
  }
}

//
// SubchannelData
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelData<SubchannelListType, SubchannelDataType>::SubchannelData(
    SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
    const ServerAddress& /*address*/,
    RefCountedPtr<SubchannelInterface> subchannel)
    : subchannel_list_(subchannel_list), subchannel_(std::move(subchannel)) {}

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelData<SubchannelListType, SubchannelDataType>::~SubchannelData() {
  GPR_ASSERT(subchannel_ == nullptr);
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::
    UnrefSubchannelLocked(const char* reason) {
  if (subchannel_ != nullptr) {
    if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
      gpr_log(GPR_INFO,
              "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): unreffing subchannel (%s)",
              subchannel_list_->tracer(), subchannel_list_->policy(),
              subchannel_list_, Index(), subchannel_list_->num_subchannels(),
              subchannel_.get(), reason);
    }
    subchannel_.reset();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::ResetBackoffLocked() {
  if (subchannel_ != nullptr) {
    subchannel_->ResetBackoff();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::
    StartConnectivityWatchLocked(const ChannelArgs& args) {
  if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
    gpr_log(
        GPR_INFO,
        "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
        " (subchannel %p): starting watch "
        "(health_check_service_name=\"%s\")",
        subchannel_list_->tracer(), subchannel_list_->policy(),
        subchannel_list_, Index(), subchannel_list_->num_subchannels(),
        subchannel_.get(),
        subchannel_list()->health_check_service_name_.value_or("N/A").c_str());
  }
  GPR_ASSERT(pending_watcher_ == nullptr);
  auto watcher = std::make_unique<Watcher>(
      this, subchannel_list()->WeakRef(DEBUG_LOCATION, "Watcher"));
  pending_watcher_ = watcher.get();
  if (subchannel_list()->health_check_service_name_.has_value()) {
    subchannel_->AddDataWatcher(MakeHealthCheckWatcher(
        subchannel_list_->work_serializer(), args, std::move(watcher)));
  } else {
    subchannel_->WatchConnectivityState(std::move(watcher));
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::
    CancelConnectivityWatchLocked(const char* reason) {
  if (pending_watcher_ != nullptr) {
    if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
      gpr_log(GPR_INFO,
              "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): canceling connectivity watch (%s)",
              subchannel_list_->tracer(), subchannel_list_->policy(),
              subchannel_list_, Index(), subchannel_list_->num_subchannels(),
              subchannel_.get(), reason);
    }
    // No need to cancel if using health checking, because the data
    // watcher will be destroyed automatically when the subchannel is.
    if (!subchannel_list()->health_check_service_name_.has_value()) {
      subchannel_->CancelConnectivityStateWatch(pending_watcher_);
    }
    pending_watcher_ = nullptr;
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::ShutdownLocked() {
  CancelConnectivityWatchLocked("shutdown");
  UnrefSubchannelLocked("shutdown");
}

//
// SubchannelList
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::SubchannelList(
    LoadBalancingPolicy* policy, const char* tracer,
    ServerAddressList addresses,
    LoadBalancingPolicy::ChannelControlHelper* helper, const ChannelArgs& args)
    : DualRefCounted<SubchannelListType>(tracer),
      policy_(policy),
      tracer_(tracer) {
  if (!args.GetBool(GRPC_ARG_INHIBIT_HEALTH_CHECKING).value_or(false)) {
    health_check_service_name_ =
        args.GetOwnedString(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME);
  }
  if (GPR_UNLIKELY(tracer_ != nullptr)) {
    gpr_log(GPR_INFO,
            "[%s %p] Creating subchannel list %p for %" PRIuPTR " subchannels",
            tracer_, policy, this, addresses.size());
  }
  subchannels_.reserve(addresses.size());
  // Create a subchannel for each address.
  for (ServerAddress address : addresses) {
    RefCountedPtr<SubchannelInterface> subchannel =
        helper->CreateSubchannel(address, args);
    if (subchannel == nullptr) {
      // Subchannel could not be created.
      if (GPR_UNLIKELY(tracer_ != nullptr)) {
        gpr_log(GPR_INFO,
                "[%s %p] could not create subchannel for address %s, ignoring",
                tracer_, policy_, address.ToString().c_str());
      }
      continue;
    }
    if (GPR_UNLIKELY(tracer_ != nullptr)) {
      gpr_log(GPR_INFO,
              "[%s %p] subchannel list %p index %" PRIuPTR
              ": Created subchannel %p for address %s",
              tracer_, policy_, this, subchannels_.size(), subchannel.get(),
              address.ToString().c_str());
    }
    subchannels_.emplace_back();
    subchannels_.back().Init(this, std::move(address), std::move(subchannel));
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::~SubchannelList() {
  if (GPR_UNLIKELY(tracer_ != nullptr)) {
    gpr_log(GPR_INFO, "[%s %p] Destroying subchannel_list %p", tracer_, policy_,
            this);
  }
  for (auto& sd : subchannels_) {
    sd.Destroy();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelList<SubchannelListType, SubchannelDataType>::
    StartWatchingLocked(const ChannelArgs& args) {
  for (auto& sd : subchannels_) {
    sd->StartConnectivityWatchLocked(args);
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelList<SubchannelListType, SubchannelDataType>::Orphan() {
  if (GPR_UNLIKELY(tracer_ != nullptr)) {
    gpr_log(GPR_INFO, "[%s %p] Shutting down subchannel_list %p", tracer_,
            policy_, this);
  }
  GPR_ASSERT(!shutting_down_);
  shutting_down_ = true;
  for (auto& sd : subchannels_) {
    sd->ShutdownLocked();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelList<SubchannelListType,
                    SubchannelDataType>::ResetBackoffLocked() {
  for (auto& sd : subchannels_) {
    sd->ResetBackoffLocked();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
bool SubchannelList<SubchannelListType,
                    SubchannelDataType>::AllSubchannelsSeenInitialState() {
  for (size_t i = 0; i < num_subchannels(); ++i) {
    if (!subchannel(i)->connectivity_state().has_value()) return false;
  }
  return true;
}

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H
