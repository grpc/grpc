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

#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/connectivity_state.h"

// Code for maintaining a list of subchannels within an LB policy.
//
// To use this, callers must create their own subclasses, like so:
/*

class MySubchannelList;  // Forward declaration.

class MySubchannelData
    : public SubchannelData<MySubchannelList, MySubchannelData> {
 public:
  void ProcessConnectivityChangeLocked(grpc_error* error) override {
    // ...code to handle connectivity changes...
  }
};

class MySubchannelList
    : public SubchannelList<MySubchannelList, MySubchannelData> {
};

*/
// All methods with a Locked() suffix must be called from within the
// client_channel combiner.

namespace grpc_core {

// Stores data for a particular subchannel in a subchannel list.
// Callers must create a subclass that implements the
// ProcessConnectivityChangeLocked() method.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelData {
 public:
  // Returns a pointer to the subchannel list containing this object.
  SubchannelListType* subchannel_list() const { return subchannel_list_; }

  // Returns the index into the subchannel list of this object.
  size_t Index() const {
    return static_cast<size_t>(static_cast<const SubchannelDataType*>(this) -
                               subchannel_list_->subchannel(0));
  }

  // Returns a pointer to the subchannel.
  grpc_subchannel* subchannel() const { return subchannel_; }

  // Returns the connected subchannel.  Will be null if the subchannel
  // is not connected.
  ConnectedSubchannel* connected_subchannel() const {
    return connected_subchannel_.get();
  }

  // The current connectivity state.
  // May be called from ProcessConnectivityChangeLocked() to determine
  // the state that the subchannel has transitioned into.
  grpc_connectivity_state connectivity_state() const {
    return curr_connectivity_state_;
  }

  // Sets the connected subchannel from the subchannel.
  void SetConnectedSubchannelFromSubchannelLocked() {
    connected_subchannel_ =
        grpc_subchannel_get_connected_subchannel(subchannel_);
  }

  // An alternative to SetConnectedSubchannelFromSubchannelLocked() for
  // cases where we are retaining a connected subchannel from a previous
  // subchannel list.  This is slightly more efficient than getting the
  // connected subchannel from the subchannel, because that approach
  // requires the use of a mutex, whereas this one only mutates a
  // refcount.
  void SetConnectedSubchannelFromLocked(SubchannelData* other) {
    GPR_ASSERT(subchannel_ == other->subchannel_);
    connected_subchannel_ = other->connected_subchannel_;  // Adds ref.
  }

  // Synchronously checks the subchannel's connectivity state.  Calls
  // ProcessConnectivityChangeLocked() if the state has changed.
  // Must not be called while there is a connectivity notification
  // pending (i.e., between calling StartConnectivityWatchLocked() and
  // the resulting invocation of ProcessConnectivityChangeLocked()).
  void CheckConnectivityStateLocked() {
    GPR_ASSERT(!connectivity_notification_pending_);
    grpc_error* error = GRPC_ERROR_NONE;
    pending_connectivity_state_unsafe_ =
        grpc_subchannel_check_connectivity(subchannel(), &error);
    if (pending_connectivity_state_unsafe_ != curr_connectivity_state_) {
      curr_connectivity_state_ = pending_connectivity_state_unsafe_;
      ProcessConnectivityChangeLocked(error);
    }
  }

  // Unrefs the subchannel.  May be used if an individual subchannel is
  // no longer needed even though the subchannel list as a whole is not
  // being unreffed.
  virtual void UnrefSubchannelLocked(const char* reason);

  // Starts or renewes watching the connectivity state of the subchannel.
  // ProcessConnectivityChangeLocked() will be called when the
  // connectivity state changes.
  void StartConnectivityWatchLocked();

  // Stops watching the connectivity state of the subchannel.
  void StopConnectivityWatchLocked();

  // Cancels watching the connectivity state of the subchannel.
  // Must be called only while there is a connectivity notification
  // pending (i.e., between calling StartConnectivityWatchLocked() and
  // the resulting invocation of ProcessConnectivityChangeLocked()).
  // From within ProcessConnectivityChangeLocked(), use
  // StopConnectivityWatchLocked() instead.
  void CancelConnectivityWatchLocked(const char* reason);

  // Cancels any pending connectivity watch and unrefs the subchannel.
  void ShutdownLocked(const char* reason);

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  SubchannelData(
      SubchannelListType* subchannel_list,
      const grpc_lb_user_data_vtable* user_data_vtable,
      const grpc_lb_address& address, grpc_subchannel* subchannel,
      grpc_combiner* combiner);

  virtual ~SubchannelData();

  // After StartConnectivityWatchLocked() is called, this method will be
  // invoked when the subchannel's connectivity state changes.
  // Implementations can use connectivity_state() to get the new
  // connectivity state.
  // Implementations must invoke either StopConnectivityWatch() or again
  // call StartConnectivityWatch() before returning.
  virtual void ProcessConnectivityChangeLocked(grpc_error* error) GRPC_ABSTRACT;

 private:
  static void OnConnectivityChangedLocked(void* arg, grpc_error* error);

  // Backpointer to owning subchannel list.  Not owned.
  SubchannelListType* subchannel_list_;

  // The subchannel and connected subchannel.
  grpc_subchannel* subchannel_;
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;

  // Notification that connectivity has changed on subchannel.
  grpc_closure connectivity_changed_closure_;
  // Is a connectivity notification pending?
  bool connectivity_notification_pending_ = false;
  // Connectivity state to be updated by
  // grpc_subchannel_notify_on_state_change(), not guarded by
  // the combiner.  Will be copied to curr_connectivity_state_ by
  // OnConnectivityChangedLocked().
  grpc_connectivity_state pending_connectivity_state_unsafe_;
  // Current connectivity state.
  grpc_connectivity_state curr_connectivity_state_;
};

// A list of subchannels.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelList
    : public RefCountedWithTracing<SubchannelListType> {
 public:
  typedef InlinedVector<SubchannelDataType, 10> SubchannelVector;

  // The number of subchannels in the list.
  size_t num_subchannels() const { return subchannels_.size(); }

  // The data for the subchannel at a particular index.
  SubchannelDataType* subchannel(size_t index) { return &subchannels_[index]; }

  // Marks the subchannel_list as discarded. Unsubscribes all its subchannels.
  void ShutdownLocked(const char* reason);

  // Returns true if the subchannel list is shutting down.
  bool shutting_down() const { return shutting_down_; }

  // Accessors.
  LoadBalancingPolicy* policy() const { return policy_; }
  TraceFlag* tracer() const { return tracer_; }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  SubchannelList(LoadBalancingPolicy* policy, TraceFlag* tracer,
                 const grpc_lb_addresses* addresses, grpc_combiner* combiner,
                 grpc_client_channel_factory* client_channel_factory,
                 const grpc_channel_args& args);

  virtual ~SubchannelList();

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* New(Args&&... args);

  // Backpointer to owning policy.
  LoadBalancingPolicy* policy_;

  TraceFlag* tracer_;

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
// SubchannelData
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelData<SubchannelListType, SubchannelDataType>::SubchannelData(
    SubchannelListType* subchannel_list,
    const grpc_lb_user_data_vtable* user_data_vtable,
    const grpc_lb_address& address, grpc_subchannel* subchannel,
    grpc_combiner* combiner)
    : subchannel_list_(subchannel_list),
      subchannel_(subchannel),
      // We assume that the current state is IDLE.  If not, we'll get a
      // callback telling us that.
      pending_connectivity_state_unsafe_(GRPC_CHANNEL_IDLE),
      curr_connectivity_state_(GRPC_CHANNEL_IDLE) {
  GRPC_CLOSURE_INIT(
      &connectivity_changed_closure_,
      (&SubchannelData<SubchannelListType,
                       SubchannelDataType>::OnConnectivityChangedLocked),
      this, grpc_combiner_scheduler(combiner));
}

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelData<SubchannelListType, SubchannelDataType>::~SubchannelData() {
  UnrefSubchannelLocked("subchannel_data_destroy");
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::UnrefSubchannelLocked(
    const char* reason) {
  if (subchannel_ != nullptr) {
    if (subchannel_list_->tracer()->enabled()) {
      gpr_log(GPR_DEBUG,
              "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): unreffing subchannel",
              subchannel_list_->tracer()->name(), subchannel_list_->policy(),
              subchannel_list_, Index(),
              subchannel_list_->num_subchannels(), subchannel_);
    }
    GRPC_SUBCHANNEL_UNREF(subchannel_, reason);
    subchannel_ = nullptr;
    connected_subchannel_.reset();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::StartConnectivityWatchLocked() {
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(
        GPR_DEBUG,
        "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
        " (subchannel %p): requesting connectivity change "
        "notification (from %s)",
        subchannel_list_->tracer()->name(), subchannel_list_->policy(),
        subchannel_list_, Index(),
        subchannel_list_->num_subchannels(), subchannel_,
        grpc_connectivity_state_name(pending_connectivity_state_unsafe_));
  }
  connectivity_notification_pending_ = true;
  grpc_subchannel_notify_on_state_change(
      subchannel_, subchannel_list_->policy()->interested_parties(),
      &pending_connectivity_state_unsafe_,
      &connectivity_changed_closure_);
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::StopConnectivityWatchLocked() {
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_DEBUG,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): stopping connectivity watch",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(),
            subchannel_list_->num_subchannels(), subchannel_);
  }
  GPR_ASSERT(connectivity_notification_pending_);
  connectivity_notification_pending_ = false;
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::CancelConnectivityWatchLocked(
    const char* reason) {
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_DEBUG,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): canceling connectivity watch (%s)",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(),
            subchannel_list_->num_subchannels(), subchannel_, reason);
  }
  grpc_subchannel_notify_on_state_change(subchannel_, nullptr, nullptr,
                                         &connectivity_changed_closure_);
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::OnConnectivityChangedLocked(
    void* arg, grpc_error* error) {
  SubchannelData* sd = static_cast<SubchannelData*>(arg);
  // Now that we're inside the combiner, copy the pending connectivity
  // state (which was set by the connectivity state watcher) to
  // curr_connectivity_state_, which is what we use inside of the combiner.
  sd->curr_connectivity_state_ = sd->pending_connectivity_state_unsafe_;
  // If we get TRANSIENT_FAILURE, unref the connected subchannel.
  if (sd->curr_connectivity_state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) {
    sd->connected_subchannel_.reset();
  }
  sd->ProcessConnectivityChangeLocked(error);
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::ShutdownLocked(const char* reason) {
  // If there's a pending notification for this subchannel, cancel it;
  // the callback is responsible for unreffing the subchannel.
  // Otherwise, unref the subchannel directly.
  if (connectivity_notification_pending_) {
    CancelConnectivityWatchLocked(reason);
  } else if (subchannel_ != nullptr) {
    UnrefSubchannelLocked(reason);
  }
}

//
// SubchannelList
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::SubchannelList(
    LoadBalancingPolicy* policy, TraceFlag* tracer,
    const grpc_lb_addresses* addresses, grpc_combiner* combiner,
    grpc_client_channel_factory* client_channel_factory,
    const grpc_channel_args& args)
    : RefCountedWithTracing<SubchannelListType>(tracer),
      policy_(policy),
      tracer_(tracer) {
  if (tracer_->enabled()) {
    gpr_log(GPR_DEBUG,
            "[%s %p] Creating subchannel list %p for %" PRIuPTR " subchannels",
            tracer_->name(), policy, this, addresses->num_addresses);
  }
  subchannels_.reserve(addresses->num_addresses);
  // We need to remove the LB addresses in order to be able to compare the
  // subchannel keys of subchannels from a different batch of addresses.
  static const char* keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS,
                                         GRPC_ARG_LB_ADDRESSES};
  // Create a subchannel for each address.
  grpc_subchannel_args sc_args;
  for (size_t i = 0; i < addresses->num_addresses; i++) {
    // If there were any balancer, we would have chosen grpclb policy instead.
    GPR_ASSERT(!addresses->addresses[i].is_balancer);
    memset(&sc_args, 0, sizeof(grpc_subchannel_args));
    grpc_arg addr_arg =
        grpc_create_subchannel_address_arg(&addresses->addresses[i].address);
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        &args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), &addr_arg, 1);
    gpr_free(addr_arg.value.string);
    sc_args.args = new_args;
    grpc_subchannel* subchannel = grpc_client_channel_factory_create_subchannel(
        client_channel_factory, &sc_args);
    grpc_channel_args_destroy(new_args);
    if (subchannel == nullptr) {
      // Subchannel could not be created.
      if (tracer_->enabled()) {
        char* address_uri =
            grpc_sockaddr_to_uri(&addresses->addresses[i].address);
        gpr_log(GPR_DEBUG,
                "[%s %p] could not create subchannel for address uri %s, "
                "ignoring",
                tracer_->name(), policy_, address_uri);
        gpr_free(address_uri);
      }
      continue;
    }
    if (tracer_->enabled()) {
      char* address_uri =
          grpc_sockaddr_to_uri(&addresses->addresses[i].address);
      gpr_log(GPR_DEBUG,
              "[%s %p] subchannel list %p index %" PRIuPTR
              ": Created subchannel %p for address uri %s",
              tracer_->name(), policy_, this, subchannels_.size(), subchannel,
              address_uri);
      gpr_free(address_uri);
    }
    subchannels_.emplace_back(static_cast<SubchannelListType*>(this),
                              addresses->user_data_vtable,
                              addresses->addresses[i], subchannel, combiner);
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::~SubchannelList() {
  if (tracer_->enabled()) {
    gpr_log(GPR_DEBUG, "[%s %p] Destroying subchannel_list %p",
            tracer_->name(), policy_, this);
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelList<SubchannelListType,
                    SubchannelDataType>::ShutdownLocked(const char* reason) {
  if (tracer_->enabled()) {
    gpr_log(GPR_DEBUG, "[%s %p] Shutting down subchannel_list %p (%s)",
            tracer_->name(), policy_, this, reason);
  }
  GPR_ASSERT(!shutting_down_);
  shutting_down_ = true;
  for (size_t i = 0; i < subchannels_.size(); i++) {
    SubchannelDataType* sd = &subchannels_[i];
    sd->ShutdownLocked(reason);
  }
}

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H */
