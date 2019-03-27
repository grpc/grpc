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
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/inlined_vector.h"
#include "src/core/lib/gprpp/orphanable.h"
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
  void ProcessConnectivityChangeLocked(
      grpc_connectivity_state connectivity_state, grpc_error* error) override {
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
  Subchannel* subchannel() const { return subchannel_; }

  // Returns the connected subchannel.  Will be null if the subchannel
  // is not connected.
  ConnectedSubchannel* connected_subchannel() const {
    return connected_subchannel_.get();
  }

  // Synchronously checks the subchannel's connectivity state.
  // Must not be called while there is a connectivity notification
  // pending (i.e., between calling StartConnectivityWatchLocked() or
  // RenewConnectivityWatchLocked() and the resulting invocation of
  // ProcessConnectivityChangeLocked()).
  grpc_connectivity_state CheckConnectivityStateLocked(grpc_error** error) {
    GPR_ASSERT(!connectivity_notification_pending_);
    pending_connectivity_state_unsafe_ = subchannel()->CheckConnectivity(
        error, subchannel_list_->inhibit_health_checking());
    UpdateConnectedSubchannelLocked();
    return pending_connectivity_state_unsafe_;
  }

  // Resets the connection backoff.
  // TODO(roth): This method should go away when we move the backoff
  // code out of the subchannel and into the LB policies.
  void ResetBackoffLocked();

  // Starts watching the connectivity state of the subchannel.
  // ProcessConnectivityChangeLocked() will be called when the
  // connectivity state changes.
  void StartConnectivityWatchLocked();

  // Renews watching the connectivity state of the subchannel.
  void RenewConnectivityWatchLocked();

  // Stops watching the connectivity state of the subchannel.
  void StopConnectivityWatchLocked();

  // Cancels watching the connectivity state of the subchannel.
  // Must be called only while there is a connectivity notification
  // pending (i.e., between calling StartConnectivityWatchLocked() or
  // RenewConnectivityWatchLocked() and the resulting invocation of
  // ProcessConnectivityChangeLocked()).
  // From within ProcessConnectivityChangeLocked(), use
  // StopConnectivityWatchLocked() instead.
  void CancelConnectivityWatchLocked(const char* reason);

  // Cancels any pending connectivity watch and unrefs the subchannel.
  void ShutdownLocked();

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  SubchannelData(
      SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
      const ServerAddress& address, Subchannel* subchannel,
      grpc_combiner* combiner);

  virtual ~SubchannelData();

  // After StartConnectivityWatchLocked() or RenewConnectivityWatchLocked()
  // is called, this method will be invoked when the subchannel's connectivity
  // state changes.
  // Implementations must invoke either RenewConnectivityWatchLocked() or
  // StopConnectivityWatchLocked() before returning.
  virtual void ProcessConnectivityChangeLocked(
      grpc_connectivity_state connectivity_state,
      grpc_error* error) GRPC_ABSTRACT;

  // Unrefs the subchannel.
  void UnrefSubchannelLocked(const char* reason);

 private:
  // Updates connected_subchannel_ based on pending_connectivity_state_unsafe_.
  // Returns true if the connectivity state should be reported.
  bool UpdateConnectedSubchannelLocked();

  static void OnConnectivityChangedLocked(void* arg, grpc_error* error);

  // Backpointer to owning subchannel list.  Not owned.
  SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list_;

  // The subchannel and connected subchannel.
  Subchannel* subchannel_;
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;

  // Notification that connectivity has changed on subchannel.
  grpc_closure connectivity_changed_closure_;
  // Is a connectivity notification pending?
  bool connectivity_notification_pending_ = false;
  // Connectivity state to be updated by
  // grpc_subchannel_notify_on_state_change(), not guarded by
  // the combiner.
  grpc_connectivity_state pending_connectivity_state_unsafe_;
};

// A list of subchannels.
template <typename SubchannelListType, typename SubchannelDataType>
class SubchannelList : public InternallyRefCounted<SubchannelListType> {
 public:
  typedef InlinedVector<SubchannelDataType, 10> SubchannelVector;

  // The number of subchannels in the list.
  size_t num_subchannels() const { return subchannels_.size(); }

  // The data for the subchannel at a particular index.
  SubchannelDataType* subchannel(size_t index) { return &subchannels_[index]; }

  // Returns true if the subchannel list is shutting down.
  bool shutting_down() const { return shutting_down_; }

  // Populates refs_list with the uuids of this SubchannelLists's subchannels.
  void PopulateChildRefsList(channelz::ChildRefsList* refs_list) {
    for (size_t i = 0; i < subchannels_.size(); ++i) {
      if (subchannels_[i].subchannel() != nullptr) {
        grpc_core::channelz::SubchannelNode* subchannel_node =
            subchannels_[i].subchannel()->channelz_node();
        if (subchannel_node != nullptr) {
          refs_list->push_back(subchannel_node->uuid());
        }
      }
    }
  }

  // Accessors.
  LoadBalancingPolicy* policy() const { return policy_; }
  TraceFlag* tracer() const { return tracer_; }
  bool inhibit_health_checking() const { return inhibit_health_checking_; }

  // Resets connection backoff of all subchannels.
  // TODO(roth): We will probably need to rethink this as part of moving
  // the backoff code out of subchannels and into LB policies.
  void ResetBackoffLocked();

  // Note: Caller must ensure that this is invoked inside of the combiner.
  void Orphan() override {
    ShutdownLocked();
    InternallyRefCounted<SubchannelListType>::Unref(DEBUG_LOCATION, "shutdown");
  }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  SubchannelList(LoadBalancingPolicy* policy, TraceFlag* tracer,
                 const ServerAddressList& addresses, grpc_combiner* combiner,
                 LoadBalancingPolicy::ChannelControlHelper* helper,
                 const grpc_channel_args& args);

  virtual ~SubchannelList();

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* New(Args&&... args);

  // For accessing Ref() and Unref().
  friend class SubchannelData<SubchannelListType, SubchannelDataType>;

  void ShutdownLocked();

  // Backpointer to owning policy.
  LoadBalancingPolicy* policy_;

  TraceFlag* tracer_;

  bool inhibit_health_checking_;

  grpc_combiner* combiner_;

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
    SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
    const ServerAddress& address, Subchannel* subchannel,
    grpc_combiner* combiner)
    : subchannel_list_(subchannel_list),
      subchannel_(subchannel),
      // We assume that the current state is IDLE.  If not, we'll get a
      // callback telling us that.
      pending_connectivity_state_unsafe_(GRPC_CHANNEL_IDLE) {
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
void SubchannelData<SubchannelListType, SubchannelDataType>::
    UnrefSubchannelLocked(const char* reason) {
  if (subchannel_ != nullptr) {
    if (subchannel_list_->tracer()->enabled()) {
      gpr_log(GPR_INFO,
              "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
              " (subchannel %p): unreffing subchannel",
              subchannel_list_->tracer()->name(), subchannel_list_->policy(),
              subchannel_list_, Index(), subchannel_list_->num_subchannels(),
              subchannel_);
    }
    GRPC_SUBCHANNEL_UNREF(subchannel_, reason);
    subchannel_ = nullptr;
    connected_subchannel_.reset();
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
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): starting watch: requesting connectivity change "
            "notification (from %s)",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_,
            grpc_connectivity_state_name(pending_connectivity_state_unsafe_));
  }
  GPR_ASSERT(!connectivity_notification_pending_);
  connectivity_notification_pending_ = true;
  subchannel_list()->Ref(DEBUG_LOCATION, "connectivity_watch").release();
  subchannel_->NotifyOnStateChange(
      subchannel_list_->policy()->interested_parties(),
      &pending_connectivity_state_unsafe_, &connectivity_changed_closure_,
      subchannel_list_->inhibit_health_checking());
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::RenewConnectivityWatchLocked() {
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): renewing watch: requesting connectivity change "
            "notification (from %s)",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_,
            grpc_connectivity_state_name(pending_connectivity_state_unsafe_));
  }
  GPR_ASSERT(connectivity_notification_pending_);
  subchannel_->NotifyOnStateChange(
      subchannel_list_->policy()->interested_parties(),
      &pending_connectivity_state_unsafe_, &connectivity_changed_closure_,
      subchannel_list_->inhibit_health_checking());
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType,
                    SubchannelDataType>::StopConnectivityWatchLocked() {
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): stopping connectivity watch",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_);
  }
  GPR_ASSERT(connectivity_notification_pending_);
  connectivity_notification_pending_ = false;
  subchannel_list()->Unref(DEBUG_LOCATION, "connectivity_watch");
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::
    CancelConnectivityWatchLocked(const char* reason) {
  if (subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): canceling connectivity watch (%s)",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_, reason);
  }
  GPR_ASSERT(connectivity_notification_pending_);
  subchannel_->NotifyOnStateChange(nullptr, nullptr,
                                   &connectivity_changed_closure_,
                                   subchannel_list_->inhibit_health_checking());
}

template <typename SubchannelListType, typename SubchannelDataType>
bool SubchannelData<SubchannelListType,
                    SubchannelDataType>::UpdateConnectedSubchannelLocked() {
  // If the subchannel is READY, take a ref to the connected subchannel.
  if (pending_connectivity_state_unsafe_ == GRPC_CHANNEL_READY) {
    connected_subchannel_ = subchannel_->connected_subchannel();
    // If the subchannel became disconnected between the time that READY
    // was reported and the time we got here (e.g., between when a
    // notification callback is scheduled and when it was actually run in
    // the combiner), then the connected subchannel may have disappeared out
    // from under us.  In that case, we don't actually want to consider the
    // subchannel to be in state READY.  Instead, we use IDLE as the
    // basis for any future connectivity watch; this is the one state that
    // the subchannel will never transition back into, so this ensures
    // that we will get a notification for the next state, even if that state
    // is READY again (e.g., if the subchannel has transitioned back to
    // READY before the next watch gets requested).
    if (connected_subchannel_ == nullptr) {
      if (subchannel_list_->tracer()->enabled()) {
        gpr_log(GPR_INFO,
                "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
                " (subchannel %p): state is READY but connected subchannel is "
                "null; moving to state IDLE",
                subchannel_list_->tracer()->name(), subchannel_list_->policy(),
                subchannel_list_, Index(), subchannel_list_->num_subchannels(),
                subchannel_);
      }
      pending_connectivity_state_unsafe_ = GRPC_CHANNEL_IDLE;
      return false;
    }
  } else {
    // For any state other than READY, unref the connected subchannel.
    connected_subchannel_.reset();
  }
  return true;
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::
    OnConnectivityChangedLocked(void* arg, grpc_error* error) {
  SubchannelData* sd = static_cast<SubchannelData*>(arg);
  if (sd->subchannel_list_->tracer()->enabled()) {
    gpr_log(
        GPR_INFO,
        "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
        " (subchannel %p): connectivity changed: state=%s, error=%s, "
        "shutting_down=%d",
        sd->subchannel_list_->tracer()->name(), sd->subchannel_list_->policy(),
        sd->subchannel_list_, sd->Index(),
        sd->subchannel_list_->num_subchannels(), sd->subchannel_,
        grpc_connectivity_state_name(sd->pending_connectivity_state_unsafe_),
        grpc_error_string(error), sd->subchannel_list_->shutting_down());
  }
  // If shutting down, unref subchannel and stop watching.
  if (sd->subchannel_list_->shutting_down() || error == GRPC_ERROR_CANCELLED) {
    sd->UnrefSubchannelLocked("connectivity_shutdown");
    sd->StopConnectivityWatchLocked();
    return;
  }
  // Get or release ref to connected subchannel.
  if (!sd->UpdateConnectedSubchannelLocked()) {
    // We don't want to report this connectivity state, so renew the watch.
    sd->RenewConnectivityWatchLocked();
    return;
  }
  // Call the subclass's ProcessConnectivityChangeLocked() method.
  sd->ProcessConnectivityChangeLocked(sd->pending_connectivity_state_unsafe_,
                                      GRPC_ERROR_REF(error));
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::ShutdownLocked() {
  // If there's a pending notification for this subchannel, cancel it;
  // the callback is responsible for unreffing the subchannel.
  // Otherwise, unref the subchannel directly.
  if (connectivity_notification_pending_) {
    CancelConnectivityWatchLocked("shutdown");
  } else if (subchannel_ != nullptr) {
    UnrefSubchannelLocked("shutdown");
  }
}

//
// SubchannelList
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::SubchannelList(
    LoadBalancingPolicy* policy, TraceFlag* tracer,
    const ServerAddressList& addresses, grpc_combiner* combiner,
    LoadBalancingPolicy::ChannelControlHelper* helper,
    const grpc_channel_args& args)
    : InternallyRefCounted<SubchannelListType>(tracer),
      policy_(policy),
      tracer_(tracer),
      combiner_(GRPC_COMBINER_REF(combiner, "subchannel_list")) {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO,
            "[%s %p] Creating subchannel list %p for %" PRIuPTR " subchannels",
            tracer_->name(), policy, this, addresses.size());
  }
  subchannels_.reserve(addresses.size());
  // We need to remove the LB addresses in order to be able to compare the
  // subchannel keys of subchannels from a different batch of addresses.
  // We also remove the inhibit-health-checking arg, since we are
  // handling that here.
  inhibit_health_checking_ = grpc_channel_arg_get_bool(
      grpc_channel_args_find(&args, GRPC_ARG_INHIBIT_HEALTH_CHECKING), false);
  static const char* keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS,
                                         GRPC_ARG_INHIBIT_HEALTH_CHECKING};
  // Create a subchannel for each address.
  for (size_t i = 0; i < addresses.size(); i++) {
    GPR_ASSERT(!addresses[i].IsBalancer());
    InlinedVector<grpc_arg, 3> args_to_add;
    const size_t subchannel_address_arg_index = args_to_add.size();
    args_to_add.emplace_back(
        Subchannel::CreateSubchannelAddressArg(&addresses[i].address()));
    if (addresses[i].args() != nullptr) {
      for (size_t j = 0; j < addresses[i].args()->num_args; ++j) {
        args_to_add.emplace_back(addresses[i].args()->args[j]);
      }
    }
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        &args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove),
        args_to_add.data(), args_to_add.size());
    gpr_free(args_to_add[subchannel_address_arg_index].value.string);
    Subchannel* subchannel = helper->CreateSubchannel(*new_args);
    grpc_channel_args_destroy(new_args);
    if (subchannel == nullptr) {
      // Subchannel could not be created.
      if (tracer_->enabled()) {
        char* address_uri = grpc_sockaddr_to_uri(&addresses[i].address());
        gpr_log(GPR_INFO,
                "[%s %p] could not create subchannel for address uri %s, "
                "ignoring",
                tracer_->name(), policy_, address_uri);
        gpr_free(address_uri);
      }
      continue;
    }
    if (tracer_->enabled()) {
      char* address_uri = grpc_sockaddr_to_uri(&addresses[i].address());
      gpr_log(GPR_INFO,
              "[%s %p] subchannel list %p index %" PRIuPTR
              ": Created subchannel %p for address uri %s",
              tracer_->name(), policy_, this, subchannels_.size(), subchannel,
              address_uri);
      gpr_free(address_uri);
    }
    subchannels_.emplace_back(this, addresses[i], subchannel, combiner);
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelList<SubchannelListType, SubchannelDataType>::~SubchannelList() {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "[%s %p] Destroying subchannel_list %p", tracer_->name(),
            policy_, this);
  }
  GRPC_COMBINER_UNREF(combiner_, "subchannel_list");
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelList<SubchannelListType, SubchannelDataType>::ShutdownLocked() {
  if (tracer_->enabled()) {
    gpr_log(GPR_INFO, "[%s %p] Shutting down subchannel_list %p",
            tracer_->name(), policy_, this);
  }
  GPR_ASSERT(!shutting_down_);
  shutting_down_ = true;
  for (size_t i = 0; i < subchannels_.size(); i++) {
    SubchannelDataType* sd = &subchannels_[i];
    sd->ShutdownLocked();
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelList<SubchannelListType,
                    SubchannelDataType>::ResetBackoffLocked() {
  for (size_t i = 0; i < subchannels_.size(); i++) {
    SubchannelDataType* sd = &subchannels_[i];
    sd->ResetBackoffLocked();
  }
}

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_SUBCHANNEL_LIST_H */
