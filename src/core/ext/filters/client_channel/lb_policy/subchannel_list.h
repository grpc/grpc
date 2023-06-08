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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/container/inlined_vector.h"

#include <grpc/impl/codegen/connectivity_state.h>
#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy.h"
#include "src/core/ext/filters/client_channel/subchannel_interface.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/resolver/server_address.h"
#include "src/core/lib/transport/connectivity_state.h"

// Code for maintaining a list of subchannels within an LB policy.
//
// To use this, callers must create their own subclasses, like so:
/*

class MySubchannelList;  // Forward declaration.

class MySubchannelData
    : public SubchannelData<MySubchannelList, MySubchannelData> {
 public:
  void ProcessConnectivityChangeLocked(
      grpc_connectivity_state connectivity_state) override {
    // ...code to handle connectivity changes...
  }
};

class MySubchannelList
    : public SubchannelList<MySubchannelList, MySubchannelData> {
};

*/
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

  // Synchronously checks the subchannel's connectivity state.
  // Must not be called while there is a connectivity notification
  // pending (i.e., between calling StartConnectivityWatchLocked() and
  // calling CancelConnectivityWatchLocked()).
  grpc_connectivity_state CheckConnectivityStateLocked() {
    GPR_ASSERT(pending_watcher_ == nullptr);
    connectivity_state_ = subchannel_->CheckConnectivityState();
    return connectivity_state_;
  }

  // Resets the connection backoff.
  // TODO(roth): This method should go away when we move the backoff
  // code out of the subchannel and into the LB policies.
  void ResetBackoffLocked();

  // Starts watching the connectivity state of the subchannel.
  // ProcessConnectivityChangeLocked() will be called whenever the
  // connectivity state changes.
  void StartConnectivityWatchLocked();

  // Cancels watching the connectivity state of the subchannel.
  void CancelConnectivityWatchLocked(const char* reason);

  // Cancels any pending connectivity watch and unrefs the subchannel.
  void ShutdownLocked();

 protected:
  SubchannelData(
      SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
      const ServerAddress& address,
      RefCountedPtr<SubchannelInterface> subchannel);

  virtual ~SubchannelData();

  // After StartConnectivityWatchLocked() is called, this method will be
  // invoked whenever the subchannel's connectivity state changes.
  // To stop watching, use CancelConnectivityWatchLocked().
  virtual void ProcessConnectivityChangeLocked(
      grpc_connectivity_state connectivity_state) = 0;

 private:
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

    void OnConnectivityStateChange(grpc_connectivity_state new_state) override;

    grpc_pollset_set* interested_parties() override {
      return subchannel_list_->policy()->interested_parties();
    }

   private:
    SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data_;
    WeakRefCountedPtr<SubchannelListType> subchannel_list_;
  };

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
  grpc_connectivity_state connectivity_state_;
};

// A list of subchannels.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelList : public DualRefCounted<SubchannelListType> {
 public:
  // We use ManualConstructor here to support SubchannelDataType classes
  // that are not copyable.
  typedef absl::InlinedVector<ManualConstructor<SubchannelDataType>, 10>
      SubchannelVector;

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
  // TODO(roth): We will probably need to rethink this as part of moving
  // the backoff code out of subchannels and into LB policies.
  void ResetBackoffLocked();

  void Orphan() override;

 protected:
  SubchannelList(LoadBalancingPolicy* policy, const char* tracer,
                 ServerAddressList addresses,
                 LoadBalancingPolicy::ChannelControlHelper* helper,
                 const grpc_channel_args& args);

  virtual ~SubchannelList();

 private:
  // For accessing Ref() and Unref().
  friend class SubchannelData<SubchannelListType, SubchannelDataType>;

  void ShutdownLocked();

  // Backpointer to owning policy.
  LoadBalancingPolicy* policy_;

  const char* tracer_;

  // The list of subchannels.
  SubchannelVector subchannels_;

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
    OnConnectivityStateChange(grpc_connectivity_state new_state) {
  if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): connectivity changed: state=%s, "
            "shutting_down=%d, pending_watcher=%p",
            subchannel_list_->tracer(), subchannel_list_->policy(),
            subchannel_list_.get(), subchannel_data_->Index(),
            subchannel_list_->num_subchannels(),
            subchannel_data_->subchannel_.get(),
            ConnectivityStateName(new_state), subchannel_list_->shutting_down(),
            subchannel_data_->pending_watcher_);
  }
  if (!subchannel_list_->shutting_down() &&
      subchannel_data_->pending_watcher_ != nullptr) {
    subchannel_data_->connectivity_state_ = new_state;
    // Call the subclass's ProcessConnectivityChangeLocked() method.
    subchannel_data_->ProcessConnectivityChangeLocked(new_state);
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
    : subchannel_list_(subchannel_list),
      subchannel_(std::move(subchannel)),
      // We assume that the current state is IDLE.  If not, we'll get a
      // callback telling us that.
      connectivity_state_(GRPC_CHANNEL_IDLE) {}

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
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::StartConnectivityWatchLocked() {
  if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): starting watch (from %s)",
            subchannel_list_->tracer(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_.get(), ConnectivityStateName(connectivity_state_));
  }
  GPR_ASSERT(pending_watcher_ == nullptr);
  pending_watcher_ =
      new Watcher(this, subchannel_list()->WeakRef(DEBUG_LOCATION, "Watcher"));
  subchannel_->WatchConnectivityState(
      connectivity_state_,
      std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>(
          pending_watcher_));
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::
    CancelConnectivityWatchLocked(const char* reason) {
  if (GPR_UNLIKELY(subchannel_list_->tracer() != nullptr)) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): canceling connectivity watch (%s)",
            subchannel_list_->tracer(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_.get(), reason);
  }
  if (pending_watcher_ != nullptr) {
    subchannel_->CancelConnectivityStateWatch(pending_watcher_);
    pending_watcher_ = nullptr;
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::ShutdownLocked() {
  if (pending_watcher_ != nullptr) CancelConnectivityWatchLocked("shutdown");
  UnrefSubchannelLocked("shutdown");
}

//
// SubchannelList
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::SubchannelList(
    LoadBalancingPolicy* policy, const char* tracer,
    ServerAddressList addresses,
    LoadBalancingPolicy::ChannelControlHelper* helper,
    const grpc_channel_args& args)
    : DualRefCounted<SubchannelListType>(tracer),
      policy_(policy),
      tracer_(tracer) {
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

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H */
