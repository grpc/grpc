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
      grpc_connectivity_state connectivity_state) override {
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
  // pending (i.e., between calling StartConnectivityWatchLocked() and
  // calling CancelConnectivityWatchLocked()).
  grpc_connectivity_state CheckConnectivityStateLocked() {
    GPR_ASSERT(pending_watcher_ == nullptr);
    connectivity_state_ = subchannel()->CheckConnectivity(
        subchannel_list_->health_check_service_name(), &connected_subchannel_);
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

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  SubchannelData(
      SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
      const ServerAddress& address, Subchannel* subchannel);

  virtual ~SubchannelData();

  // After StartConnectivityWatchLocked() is called, this method will be
  // invoked whenever the subchannel's connectivity state changes.
  // To stop watching, use CancelConnectivityWatchLocked().
  virtual void ProcessConnectivityChangeLocked(
      grpc_connectivity_state connectivity_state) GRPC_ABSTRACT;

 private:
  // Watcher for subchannel connectivity state.
  class Watcher : public Subchannel::ConnectivityStateWatcher {
   public:
    Watcher(
        SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data,
        RefCountedPtr<SubchannelListType> subchannel_list)
        : subchannel_data_(subchannel_data),
          subchannel_list_(std::move(subchannel_list)) {}

    ~Watcher() { subchannel_list_.reset(DEBUG_LOCATION, "Watcher dtor"); }

    void OnConnectivityStateChange(
        grpc_connectivity_state new_state,
        RefCountedPtr<ConnectedSubchannel> connected_subchannel) override;

    grpc_pollset_set* interested_parties() override {
      return subchannel_list_->policy()->interested_parties();
    }

   private:
    // A fire-and-forget class that bounces into the combiner to process
    // a connectivity state update.
    class Updater {
     public:
      Updater(
          SubchannelData<SubchannelListType, SubchannelDataType>*
              subchannel_data,
          RefCountedPtr<SubchannelList<SubchannelListType, SubchannelDataType>>
              subchannel_list,
          grpc_connectivity_state state,
          RefCountedPtr<ConnectedSubchannel> connected_subchannel);

      ~Updater() {
        subchannel_list_.reset(DEBUG_LOCATION, "Watcher::Updater dtor");
      }

     private:
      static void OnUpdate(void* arg, grpc_error* error);

      SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data_;
      RefCountedPtr<SubchannelList<SubchannelListType, SubchannelDataType>>
          subchannel_list_;
      const grpc_connectivity_state state_;
      RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
      grpc_closure closure_;
    };

    SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data_;
    RefCountedPtr<SubchannelListType> subchannel_list_;
  };

  // Unrefs the subchannel.
  void UnrefSubchannelLocked(const char* reason);

  // Backpointer to owning subchannel list.  Not owned.
  SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list_;
  // The subchannel.
  Subchannel* subchannel_;
  // Will be non-null when the subchannel's state is being watched.
  Subchannel::ConnectivityStateWatcher* pending_watcher_ = nullptr;
  // Data updated by the watcher.
  grpc_connectivity_state connectivity_state_;
  RefCountedPtr<ConnectedSubchannel> connected_subchannel_;
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
  const char* health_check_service_name() const {
    return health_check_service_name_.get();
  }

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

  UniquePtr<char> health_check_service_name_;

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
// SubchannelData::Watcher
//

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::Watcher::
    OnConnectivityStateChange(
        grpc_connectivity_state new_state,
        RefCountedPtr<ConnectedSubchannel> connected_subchannel) {
  // Will delete itself.
  New<Updater>(subchannel_data_,
               subchannel_list_->Ref(DEBUG_LOCATION, "Watcher::Updater"),
               new_state, std::move(connected_subchannel));
}

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelData<SubchannelListType, SubchannelDataType>::Watcher::Updater::
    Updater(
        SubchannelData<SubchannelListType, SubchannelDataType>* subchannel_data,
        RefCountedPtr<SubchannelList<SubchannelListType, SubchannelDataType>>
            subchannel_list,
        grpc_connectivity_state state,
        RefCountedPtr<ConnectedSubchannel> connected_subchannel)
    : subchannel_data_(subchannel_data),
      subchannel_list_(std::move(subchannel_list)),
      state_(state),
      connected_subchannel_(std::move(connected_subchannel)) {
  GRPC_CLOSURE_INIT(&closure_, &OnUpdate, this,
                    grpc_combiner_scheduler(subchannel_list_->combiner_));
  GRPC_CLOSURE_SCHED(&closure_, GRPC_ERROR_NONE);
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::Watcher::Updater::
    OnUpdate(void* arg, grpc_error* error) {
  Updater* self = static_cast<Updater*>(arg);
  SubchannelData* sd = self->subchannel_data_;
  if (sd->subchannel_list_->tracer()->enabled()) {
    gpr_log(GPR_INFO,
            "[%s %p] subchannel list %p index %" PRIuPTR " of %" PRIuPTR
            " (subchannel %p): connectivity changed: state=%s, "
            "connected_subchannel=%p, shutting_down=%d",
            sd->subchannel_list_->tracer()->name(),
            sd->subchannel_list_->policy(), sd->subchannel_list_, sd->Index(),
            sd->subchannel_list_->num_subchannels(), sd->subchannel_,
            grpc_connectivity_state_name(self->state_),
            self->connected_subchannel_.get(),
            sd->subchannel_list_->shutting_down());
  }
  if (!sd->subchannel_list_->shutting_down()) {
    sd->connectivity_state_ = self->state_;
    // Get or release ref to connected subchannel.
    sd->connected_subchannel_ = std::move(self->connected_subchannel_);
    // Call the subclass's ProcessConnectivityChangeLocked() method.
    sd->ProcessConnectivityChangeLocked(sd->connectivity_state_);
  }
  // Clean up.
  Delete(self);
}

//
// SubchannelData
//

template <typename SubchannelListType, typename SubchannelDataType>
SubchannelData<SubchannelListType, SubchannelDataType>::SubchannelData(
    SubchannelList<SubchannelListType, SubchannelDataType>* subchannel_list,
    const ServerAddress& address, Subchannel* subchannel)
    : subchannel_list_(subchannel_list),
      subchannel_(subchannel),
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
            " (subchannel %p): starting watch (from %s)",
            subchannel_list_->tracer()->name(), subchannel_list_->policy(),
            subchannel_list_, Index(), subchannel_list_->num_subchannels(),
            subchannel_, grpc_connectivity_state_name(connectivity_state_));
  }
  GPR_ASSERT(pending_watcher_ == nullptr);
  pending_watcher_ =
      New<Watcher>(this, subchannel_list()->Ref(DEBUG_LOCATION, "Watcher"));
  subchannel_->WatchConnectivityState(
      connectivity_state_,
      UniquePtr<char>(
          gpr_strdup(subchannel_list_->health_check_service_name())),
      UniquePtr<Subchannel::ConnectivityStateWatcher>(pending_watcher_));
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
  if (pending_watcher_ != nullptr) {
    subchannel_->CancelConnectivityStateWatch(
        subchannel_list_->health_check_service_name(), pending_watcher_);
    pending_watcher_ = nullptr;
  }
}

template <typename SubchannelListType, typename SubchannelDataType>
void SubchannelData<SubchannelListType, SubchannelDataType>::ShutdownLocked() {
  if (pending_watcher_ != nullptr) {
    CancelConnectivityWatchLocked("shutdown");
  }
  UnrefSubchannelLocked("shutdown");
}

//
// SubchannelList
//

// TODO(roth): Move this into the client channel service config parsing
// code as part of merging in the service config error handling changes.
struct HealthCheckParams {
  UniquePtr<char> service_name;

  static void Parse(const grpc_json* field, HealthCheckParams* params) {
    if (strcmp(field->key, "healthCheckConfig") == 0) {
      if (field->type != GRPC_JSON_OBJECT) return;
      for (grpc_json* sub_field = field->child; sub_field != nullptr;
           sub_field = sub_field->next) {
        if (sub_field->key == nullptr) return;
        if (strcmp(sub_field->key, "serviceName") == 0) {
          if (params->service_name != nullptr) return;  // Duplicate.
          if (sub_field->type != GRPC_JSON_STRING) return;
          params->service_name.reset(gpr_strdup(sub_field->value));
        }
      }
    }
  }
};

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
  // Find health check service name.
  const bool inhibit_health_checking = grpc_channel_arg_get_bool(
      grpc_channel_args_find(&args, GRPC_ARG_INHIBIT_HEALTH_CHECKING), false);
  if (!inhibit_health_checking) {
    const char* service_config_json = grpc_channel_arg_get_string(
        grpc_channel_args_find(&args, GRPC_ARG_SERVICE_CONFIG));
    if (service_config_json != nullptr) {
      grpc_error* service_config_error = GRPC_ERROR_NONE;
      RefCountedPtr<ServiceConfig> service_config =
          ServiceConfig::Create(service_config_json, &service_config_error);
      // service_config_error is currently unused.
      GRPC_ERROR_UNREF(service_config_error);
      if (service_config != nullptr) {
        HealthCheckParams params;
        service_config->ParseGlobalParams(HealthCheckParams::Parse, &params);
        health_check_service_name_ = std::move(params.service_name);
      }
    }
  }
  // We need to remove the LB addresses in order to be able to compare the
  // subchannel keys of subchannels from a different batch of addresses.
  // We also remove the inhibit-health-checking arg, since we are
  // handling that here.
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
    subchannels_.emplace_back(this, addresses[i], subchannel);
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
