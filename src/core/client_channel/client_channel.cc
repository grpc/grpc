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

#include "src/core/ext/filters/client_channel/client_channel.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <functional>
#include <new>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/json.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/backend_metric.h"
#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/client_channel_internal.h"
#include "src/core/ext/filters/client_channel/client_channel_service_config.h"
#include "src/core/ext/filters/client_channel/config_selector.h"
#include "src/core/ext/filters/client_channel/dynamic_filters.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/lb_policy/child_policy_handler.h"
#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/retry_filter.h"
#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/ext/filters/client_channel/subchannel_interface_internal.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/handshaker/proxy_mapper_registry.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/load_balancing/lb_policy_registry.h"
#include "src/core/lib/load_balancing/subchannel_interface.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resolver/endpoint_addresses.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/service_config/service_config_call_data.h"
#include "src/core/lib/service_config/service_config_impl.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"

#include "src/core/lib/channel/promise_based_filter.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

using internal::ClientChannelMethodParsedConfig;

// FIXME: share with legacy client channel impl
TraceFlag grpc_client_channel_trace(false, "client_channel");
TraceFlag grpc_client_channel_call_trace(false, "client_channel_call");
TraceFlag grpc_client_channel_lb_call_trace(false, "client_channel_lb_call");

//
// ClientChannel::ResolverResultHandler
//

class ClientChannel::ResolverResultHandler : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(RefCountedPtr<ClientChannel> client_channel)
      : client_channel_(std::move(client_channel)) {}

  ~ResolverResultHandler() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: resolver shutdown complete",
              client_channel_.get());
    }
  }

  void ReportResult(Resolver::Result result) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    client_channel_->OnResolverResultChangedLocked(std::move(result));
  }

 private:
  RefCountedPtr<ClientChannel> client_channel_;
};

//
// ClientChannel::SubchannelWrapper
//

// This class is a wrapper for Subchannel that hides details of the
// channel's implementation (such as the connected subchannel) from the
// LB policy API.
//
// Note that no synchronization is needed here, because even if the
// underlying subchannel is shared between channels, this wrapper will only
// be used within one channel, so it will always be synchronized by the
// control plane work_serializer.
class ClientChannel::SubchannelWrapper : public SubchannelInterface {
 public:
  SubchannelWrapper(RefCountedPtr<ClientChannel> client_channel,
                    RefCountedPtr<Subchannel> subchannel)
      : SubchannelInterface(GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)
                                ? "SubchannelWrapper"
                                : nullptr),
        client_channel_(std::move(client_channel)),
        subchannel_(std::move(subchannel)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO,
              "client_channel=%p: creating subchannel wrapper %p for subchannel %p",
              client_channel_.get(), this, subchannel_.get());
    }
    GPR_DEBUG_ASSERT(
        client_channel_->work_serializer_->RunningInWorkSerializer());
    if (client_channel_->channelz_node_ != nullptr) {
      auto* subchannel_node = subchannel_->channelz_node();
      if (subchannel_node != nullptr) {
        auto it =
            client_channel_->subchannel_refcount_map_.find(subchannel_.get());
        if (it == client_channel_->subchannel_refcount_map_.end()) {
          client_channel_->channelz_node_->AddChildSubchannel(
              subchannel_node->uuid());
          it = client_channel_->subchannel_refcount_map_.emplace(
              subchannel_.get(), 0)
              .first;
        }
        ++it->second;
      }
    }
    client_channel_->subchannel_wrappers_.insert(this);
  }

  ~SubchannelWrapper() override {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO,
              "client_channel=%p: destroying subchannel wrapper %p for subchannel %p",
              client_channel_, this, subchannel_.get());
    }
  }

  void Orphan() override {
    // Make sure we clean up the channel's subchannel maps inside the
    // WorkSerializer.
    client_channel_->work_serializer_->Run(
        [self = WeakRef(DEBUG_LOCATION, "subchannel map cleanup")]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
          self->client_channel_->subchannel_wrappers_.erase(this);
          if (self->client_channel_->channelz_node_ != nullptr) {
            auto* subchannel_node = self->subchannel_->channelz_node();
            if (subchannel_node != nullptr) {
              auto it = self->client_channel_->subchannel_refcount_map_.find(
                  subchannel_.get());
              GPR_ASSERT(
                  it != self->client_channel_->subchannel_refcount_map_.end());
              --it->second;
              if (it->second == 0) {
                self->client_channel_->channelz_node_->RemoveChildSubchannel(
                    subchannel_node->uuid());
                self->client_channel_->subchannel_refcount_map_.erase(it);
              }
            }
          }
        },
        DEBUG_LOCATION);
  }

  void WatchConnectivityState(
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    auto& watcher_wrapper = watcher_map_[watcher.get()];
    GPR_ASSERT(watcher_wrapper == nullptr);
    watcher_wrapper = new WatcherWrapper(
        std::move(watcher),
        RefAsSubclass<SubchannelWrapper>(DEBUG_LOCATION, "WatcherWrapper"));
    subchannel_->WatchConnectivityState(
        RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface>(
            watcher_wrapper));
  }

  void CancelConnectivityStateWatch(ConnectivityStateWatcherInterface* watcher)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    auto it = watcher_map_.find(watcher);
    GPR_ASSERT(it != watcher_map_.end());
    subchannel_->CancelConnectivityStateWatch(it->second);
    watcher_map_.erase(it);
  }

  RefCountedPtr<ConnectedSubchannel> connected_subchannel() const {
    return subchannel_->connected_subchannel();
  }

  void RequestConnection() override { subchannel_->RequestConnection(); }

  void ResetBackoff() override { subchannel_->ResetBackoff(); }

  void AddDataWatcher(std::unique_ptr<DataWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    static_cast<InternalSubchannelDataWatcherInterface*>(watcher.get())
        ->SetSubchannel(subchannel_.get());
    GPR_ASSERT(data_watchers_.insert(std::move(watcher)).second);
  }

  void CancelDataWatcher(DataWatcherInterface* watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    auto it = data_watchers_.find(watcher);
    if (it != data_watchers_.end()) data_watchers_.erase(it);
  }

  void ThrottleKeepaliveTime(int new_keepalive_time) {
    subchannel_->ThrottleKeepaliveTime(new_keepalive_time);
  }

 private:
  // This wrapper provides a bridge between the internal Subchannel API
  // and the SubchannelInterface API that we expose to LB policies.
  // It implements Subchannel::ConnectivityStateWatcherInterface and wraps
  // the instance of SubchannelInterface::ConnectivityStateWatcherInterface
  // that was passed in by the LB policy.  We pass an instance of this
  // class to the underlying Subchannel, and when we get updates from
  // the subchannel, we pass those on to the wrapped watcher to return
  // the update to the LB policy.
  //
  // This class handles things like hopping into the WorkSerializer
  // before passing notifications to the LB policy and propagating
  // keepalive information betwen subchannels.
  class WatcherWrapper : public Subchannel::ConnectivityStateWatcherInterface {
   public:
    WatcherWrapper(
        std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
            watcher,
        RefCountedPtr<SubchannelWrapper> subchannel_wrapper)
        : watcher_(std::move(watcher)),
          subchannel_wrapper_(std::move(subchannel_wrapper)) {}

    ~WatcherWrapper() override {
      subchannel_wrapper_.reset(DEBUG_LOCATION, "WatcherWrapper");
    }

    void OnConnectivityStateChange(
        RefCountedPtr<ConnectivityStateWatcherInterface> self,
        grpc_connectivity_state state, const absl::Status& status) override {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "client_channel=%p: connectivity change for subchannel wrapper %p "
                "subchannel %p; hopping into work_serializer",
                subchannel_wrapper_->client_channel_.get(),
                subchannel_wrapper_.get(),
                subchannel_wrapper_->subchannel_.get());
      }
      subchannel_wrapper_->client_channel_->work_serializer_->Run(
          [self = std::move(self), state, status]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(
              *subchannel_wrapper_->client_channel_->work_serializer_) {
            self->ApplyUpdateInControlPlaneWorkSerializer(state, status);
          },
          DEBUG_LOCATION);
    }

    grpc_pollset_set* interested_parties() override {
      return watcher_->interested_parties();
    }

   private:
    void ApplyUpdateInControlPlaneWorkSerializer(grpc_connectivity_state state,
                                                 const absl::Status& status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(*subchannel_wrapper_->client_channel_->work_serializer_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "client_channel=%p: processing connectivity change in work serializer "
                "for subchannel wrapper %p subchannel %p watcher=%p "
                "state=%s status=%s",
                subchannel_wrapper_->client_channel_.get(),
                subchannel_wrapper_.get(),
                subchannel_wrapper_->subchannel_.get(), watcher_.get(),
                ConnectivityStateName(state), status.ToString().c_str());
      }
      absl::optional<absl::Cord> keepalive_throttling =
          status.GetPayload(kKeepaliveThrottlingKey);
      if (keepalive_throttling.has_value()) {
        int new_keepalive_time = -1;
        if (absl::SimpleAtoi(std::string(keepalive_throttling.value()),
                             &new_keepalive_time)) {
          if (new_keepalive_time > subchannel_wrapper_->client_channel_->keepalive_time_) {
            subchannel_wrapper_->client_channel_->keepalive_time_ = new_keepalive_time;
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
              gpr_log(GPR_INFO, "client_channel=%p: throttling keepalive time to %d",
                      subchannel_wrapper_->client_channel_.get(),
                      subchannel_wrapper_->client_channel_->keepalive_time_);
            }
            // Propagate the new keepalive time to all subchannels. This is so
            // that new transports created by any subchannel (and not just the
            // subchannel that received the GOAWAY), use the new keepalive time.
            for (auto* subchannel_wrapper :
                 subchannel_wrapper_->client_channel_->subchannel_wrappers_) {
              subchannel_wrapper->ThrottleKeepaliveTime(new_keepalive_time);
            }
          }
        } else {
          gpr_log(GPR_ERROR, "client_channel=%p: Illegal keepalive throttling value %s",
                  subchannel_wrapper_->client_channel_.get(),
                  std::string(keepalive_throttling.value()).c_str());
        }
      }
      // Propagate status only in state TF.
      // We specifically want to avoid propagating the status for
      // state IDLE that the real subchannel gave us only for the
      // purpose of keepalive propagation.
      watcher_->OnConnectivityStateChange(
          state,
          state == GRPC_CHANNEL_TRANSIENT_FAILURE ? status : absl::OkStatus());
    }

    std::unique_ptr<SubchannelInterface::ConnectivityStateWatcherInterface>
        watcher_;
    RefCountedPtr<SubchannelWrapper> subchannel_wrapper_;
  };

  // A heterogenous lookup comparator for data watchers that allows
  // unique_ptr keys to be looked up as raw pointers.
  struct DataWatcherLessThan {
    using is_transparent = void;
    bool operator()(const std::unique_ptr<DataWatcherInterface>& p1,
                    const std::unique_ptr<DataWatcherInterface>& p2) const {
      return p1 < p2;
    }
    bool operator()(const std::unique_ptr<DataWatcherInterface>& p1,
                    const DataWatcherInterface* p2) const {
      return p1.get() < p2;
    }
    bool operator()(const DataWatcherInterface* p1,
                    const std::unique_ptr<DataWatcherInterface>& p2) const {
      return p1 < p2.get();
    }
  };

  RefCountedPtr<ClientChannel> client_channel_;
  RefCountedPtr<Subchannel> subchannel_;
  // Maps from the address of the watcher passed to us by the LB policy
  // to the address of the WrapperWatcher that we passed to the underlying
  // subchannel.  This is needed so that when the LB policy calls
  // CancelConnectivityStateWatch() with its watcher, we know the
  // corresponding WrapperWatcher to cancel on the underlying subchannel.
  std::map<ConnectivityStateWatcherInterface*, WatcherWrapper*> watcher_map_
      ABSL_GUARDED_BY(*client_channel_->work_serializer_);
  std::set<std::unique_ptr<DataWatcherInterface>, DataWatcherLessThan>
      data_watchers_ ABSL_GUARDED_BY(*client_channel_->work_serializer_);
};

// FIXME: figure this out
#if 0
//
// ClientChannel::ExternalConnectivityWatcher
//

ClientChannel::ExternalConnectivityWatcher::ExternalConnectivityWatcher(
    ClientChannel* client_channel, grpc_polling_entity pollent,
    grpc_connectivity_state* state, grpc_closure* on_complete,
    grpc_closure* watcher_timer_init)
    : client_channel_(client_channel),
      pollent_(pollent),
      initial_state_(*state),
      state_(state),
      on_complete_(on_complete),
      watcher_timer_init_(watcher_timer_init) {
  grpc_polling_entity_add_to_pollset_set(&pollent_,
                                         client_channel_->interested_parties_);
  GRPC_CHANNEL_STACK_REF(client_channel_->owning_stack_, "ExternalConnectivityWatcher");
  {
    MutexLock lock(&client_channel_->external_watchers_mu_);
    // Will be deleted when the watch is complete.
    GPR_ASSERT(client_channel->external_watchers_[on_complete] == nullptr);
    // Store a ref to the watcher in the external_watchers_ map.
    client_channel->external_watchers_[on_complete] =
        RefAsSubclass<ExternalConnectivityWatcher>(
            DEBUG_LOCATION, "AddWatcherToExternalWatchersMapLocked");
  }
  // Pass the ref from creating the object to Start().
  client_channel_->work_serializer_->Run(
      [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
        // The ref is passed to AddWatcherLocked().
        AddWatcherLocked();
      },
      DEBUG_LOCATION);
}

ClientChannel::ExternalConnectivityWatcher::
    ~ExternalConnectivityWatcher() {
  grpc_polling_entity_del_from_pollset_set(&pollent_,
                                           client_channel_->interested_parties_);
  GRPC_CHANNEL_STACK_UNREF(client_channel_->owning_stack_,
                           "ExternalConnectivityWatcher");
}

void ClientChannel::ExternalConnectivityWatcher::
    RemoveWatcherFromExternalWatchersMap(ClientChannel* client_channel,
                                         grpc_closure* on_complete,
                                         bool cancel) {
  RefCountedPtr<ExternalConnectivityWatcher> watcher;
  {
    MutexLock lock(&client_channel->external_watchers_mu_);
    auto it = client_channel->external_watchers_.find(on_complete);
    if (it != client_channel->external_watchers_.end()) {
      watcher = std::move(it->second);
      client_channel->external_watchers_.erase(it);
    }
  }
  // watcher->Cancel() will hop into the WorkSerializer, so we have to unlock
  // the mutex before calling it.
  if (watcher != nullptr && cancel) watcher->Cancel();
}

void ClientChannel::ExternalConnectivityWatcher::Notify(
    grpc_connectivity_state state, const absl::Status& /* status */) {
  bool done = false;
  if (!done_.compare_exchange_strong(done, true, std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
    return;  // Already done.
  }
  // Remove external watcher.
  ExternalConnectivityWatcher::RemoveWatcherFromExternalWatchersMap(
      client_channel_, on_complete_, /*cancel=*/false);
  // Report new state to the user.
  *state_ = state;
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, absl::OkStatus());
  // Hop back into the work_serializer to clean up.
  // Not needed in state SHUTDOWN, because the tracker will
  // automatically remove all watchers in that case.
  // Note: The callback takes a ref in case the ref inside the state tracker
  // gets removed before the callback runs via a SHUTDOWN notification.
  if (state != GRPC_CHANNEL_SHUTDOWN) {
    Ref(DEBUG_LOCATION, "RemoveWatcherLocked()").release();
    client_channel_->work_serializer_->Run(
        [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
          RemoveWatcherLocked();
          Unref(DEBUG_LOCATION, "RemoveWatcherLocked()");
        },
        DEBUG_LOCATION);
  }
}

void ClientChannel::ExternalConnectivityWatcher::Cancel() {
  bool done = false;
  if (!done_.compare_exchange_strong(done, true, std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
    return;  // Already done.
  }
  ExecCtx::Run(DEBUG_LOCATION, on_complete_, absl::CancelledError());
  // Hop back into the work_serializer to clean up.
  // Note: The callback takes a ref in case the ref inside the state tracker
  // gets removed before the callback runs via a SHUTDOWN notification.
  Ref(DEBUG_LOCATION, "RemoveWatcherLocked()").release();
  client_channel_->work_serializer_->Run(
      [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
        RemoveWatcherLocked();
        Unref(DEBUG_LOCATION, "RemoveWatcherLocked()");
      },
      DEBUG_LOCATION);
}

void ClientChannel::ExternalConnectivityWatcher::AddWatcherLocked() {
  Closure::Run(DEBUG_LOCATION, watcher_timer_init_, absl::OkStatus());
  // Add new watcher. Pass the ref of the object from creation to OrphanablePtr.
  client_channel_->state_tracker_.AddWatcher(
      initial_state_, OrphanablePtr<ConnectivityStateWatcherInterface>(this));
}

void ClientChannel::ExternalConnectivityWatcher::RemoveWatcherLocked() {
  client_channel_->state_tracker_.RemoveWatcher(this);
}
#endif

//
// ClientChannel::ClientChannelControlHelper
//

class ClientChannel::ClientChannelControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ClientChannelControlHelper(
      RefCountedPtr<ClientChannel> client_channel)
      : client_channel_(std::move(client_channel)) {}

  ~ClientChannelControlHelper() override {
    client_channel_.reset(DEBUG_LOCATION, "ClientChannelControlHelper");
  }

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& per_address_args,
      const ChannelArgs& args) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    if (client_channel_->resolver_ == nullptr) return nullptr;  // Shutting down.
    ChannelArgs subchannel_args = ClientChannel::MakeSubchannelArgs(
        args, per_address_args, client_channel_->subchannel_pool_,
        client_channel_->default_authority_);
    // Create subchannel.
    RefCountedPtr<Subchannel> subchannel =
        client_channel_->client_channel_factory_->CreateSubchannel(
            address, subchannel_args);
    if (subchannel == nullptr) return nullptr;
    // Make sure the subchannel has updated keepalive time.
    subchannel->ThrottleKeepaliveTime(client_channel_->keepalive_time_);
    // Create and return wrapper for the subchannel.
    return MakeRefCounted<SubchannelWrapper>(client_channel_,
                                             std::move(subchannel));
  }

  void UpdateState(grpc_connectivity_state state, const absl::Status& status,
                   RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    if (client_channel_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      const char* extra = client_channel_->disconnect_error_.ok()
                              ? ""
                              : " (ignoring -- channel shutting down)";
      gpr_log(GPR_INFO, "client_channel=%p: update: state=%s status=(%s) picker=%p%s",
              client_channel_.get(), ConnectivityStateName(state),
              status.ToString().c_str(), picker.get(), extra);
    }
    // Do update only if not shutting down.
    if (client_channel_->disconnect_error_.ok()) {
      client_channel_->UpdateStateAndPickerLocked(state, status, "helper",
                                                  std::move(picker));
    }
  }

  void RequestReresolution() override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    if (client_channel_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: started name re-resolving",
              client_channel_.get());
    }
    client_channel_->resolver_->RequestReresolutionLocked();
  }

  absl::string_view GetAuthority() override {
    return client_channel_->default_authority_;
  }

  RefCountedPtr<grpc_channel_credentials> GetChannelCredentials() override {
    return client_channel_->channel_args_.GetObject<grpc_channel_credentials>()
        ->duplicate_without_call_credentials();
  }

  RefCountedPtr<grpc_channel_credentials> GetUnsafeChannelCredentials()
      override {
    return client_channel_->channel_args_.GetObject<grpc_channel_credentials>()->Ref();
  }

  grpc_event_engine::experimental::EventEngine* GetEventEngine() override {
    return client_channel_->owning_stack_->EventEngine();
  }

  void AddTraceEvent(TraceSeverity severity, absl::string_view message) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    if (client_channel_->resolver_ == nullptr) return;  // Shutting down.
    if (client_channel_->channelz_node_ != nullptr) {
      client_channel_->channelz_node_->AddTraceEvent(
          ConvertSeverityEnum(severity),
          grpc_slice_from_copied_buffer(message.data(), message.size()));
    }
  }

 private:
  static channelz::ChannelTrace::Severity ConvertSeverityEnum(
      TraceSeverity severity) {
    if (severity == TRACE_INFO) return channelz::ChannelTrace::Info;
    if (severity == TRACE_WARNING) return channelz::ChannelTrace::Warning;
    return channelz::ChannelTrace::Error;
  }

  RefCountedPtr<ClientChannel> client_channel_;
};

//
// ClientChannel implementation
//

ClientChannel* ClientChannel::GetFromChannel(Channel* channel) {
// FIXME: implement this
  return nullptr;
}

namespace {

RefCountedPtr<SubchannelPoolInterface> GetSubchannelPool(
    const ChannelArgs& args) {
  if (args.GetBool(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL).value_or(false)) {
    return MakeRefCounted<LocalSubchannelPool>();
  }
  return GlobalSubchannelPool::instance();
}

}  // namespace

ClientChannel::ClientChannel(absl::string_view target_uri,
                             ChannelArgs channel_args)
    : channel_args_(std::move(channel_args)),
      client_channel_factory_(channel_args_.GetObject<ClientChannelFactory>()),
      channelz_node_(channel_args_.GetObject<channelz::ChannelNode>()),
      service_config_parser_index_(
          internal::ClientChannelServiceConfigParser::ParserIndex()),
      work_serializer_(
          std::make_shared<WorkSerializer>(GetContext<EventEngine>()),
      state_tracker_("client_channel", GRPC_CHANNEL_IDLE),
      subchannel_pool_(GetSubchannelPool(channel_args_)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: creating client_channel", this);
  }
  // Check client channel factory.
  if (client_channel_factory_ == nullptr) {
    *error = GRPC_ERROR_CREATE(
        "Missing client channel factory in args for client channel filter");
    return;
  }
// FIXME: figure out if this should be done in the channel instead of
// here, and what data should be passed into this ctor
  // Get default service config.  If none is specified via the client API,
  // we use an empty config.
  absl::optional<absl::string_view> service_config_json =
      channel_args_.GetString(GRPC_ARG_SERVICE_CONFIG);
  if (!service_config_json.has_value()) service_config_json = "{}";
  *error = absl::OkStatus();
  auto service_config =
      ServiceConfigImpl::Create(channel_args_, *service_config_json);
  if (!service_config.ok()) {
    *error = absl_status_to_grpc_error(service_config.status());
    return;
  }
  default_service_config_ = std::move(*service_config);
  // Get URI to resolve, using proxy mapper if needed.
  absl::optional<std::string> server_uri =
      channel_args_.GetOwnedString(GRPC_ARG_SERVER_URI);
  if (!server_uri.has_value()) {
    *error = GRPC_ERROR_CREATE(
        "target URI channel arg missing or wrong type in client channel "
        "filter");
    return;
  }
  uri_to_resolve_ = CoreConfiguration::Get()
                        .proxy_mapper_registry()
                        .MapName(*server_uri, &channel_args_)
                        .value_or(*server_uri);
  // Make sure the URI to resolve is valid, so that we know that
  // resolver creation will succeed later.
  if (!CoreConfiguration::Get().resolver_registry().IsValidTarget(
          uri_to_resolve_)) {
    *error = GRPC_ERROR_CREATE(
        absl::StrCat("the target uri is not valid: ", uri_to_resolve_));
    return;
  }
  // Strip out service config channel arg, so that it doesn't affect
  // subchannel uniqueness when the args flow down to that layer.
  channel_args_ = channel_args_.Remove(GRPC_ARG_SERVICE_CONFIG);
  // Set initial keepalive time.
  auto keepalive_arg = channel_args_.GetInt(GRPC_ARG_KEEPALIVE_TIME_MS);
  if (keepalive_arg.has_value()) {
    keepalive_time_ = Clamp(*keepalive_arg, 1, INT_MAX);
  } else {
    keepalive_time_ = -1;  // unset
  }
  // Set default authority.
  absl::optional<std::string> default_authority =
      channel_args_.GetOwnedString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (!default_authority.has_value()) {
    default_authority_ =
        CoreConfiguration::Get().resolver_registry().GetDefaultAuthority(
            *server_uri);
  } else {
    default_authority_ = std::move(*default_authority);
  }
  // Success.
  *error = absl::OkStatus();
}

ClientChannel::~ClientChannel() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: destroying channel", this);
  }
  DestroyResolverAndLbPolicyLocked();
}

// FIXME: move to Subchannel?
ChannelArgs ClientChannel::MakeSubchannelArgs(
    const ChannelArgs& channel_args, const ChannelArgs& address_args,
    const RefCountedPtr<SubchannelPoolInterface>& subchannel_pool,
    const std::string& channel_default_authority) {
  // Note that we start with the channel-level args and then apply the
  // per-address args, so that if a value is present in both, the one
  // in the channel-level args is used.  This is particularly important
  // for the GRPC_ARG_DEFAULT_AUTHORITY arg, which we want to allow
  // resolvers to set on a per-address basis only if the application
  // did not explicitly set it at the channel level.
  return channel_args.UnionWith(address_args)
      .SetObject(subchannel_pool)
      // If we haven't already set the default authority arg (i.e., it
      // was not explicitly set by the application nor overridden by
      // the resolver), add it from the channel's default.
      .SetIfUnset(GRPC_ARG_DEFAULT_AUTHORITY, channel_default_authority)
      // Remove channel args that should not affect subchannel
      // uniqueness.
      .Remove(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME)
      .Remove(GRPC_ARG_INHIBIT_HEALTH_CHECKING)
      .Remove(GRPC_ARG_CHANNELZ_CHANNEL_NODE)
      // Remove all keys with the no-subchannel prefix.
      .RemoveAllKeysWithPrefix(GRPC_ARG_NO_SUBCHANNEL_PREFIX);
}

namespace {

RefCountedPtr<LoadBalancingPolicy::Config> ChooseLbPolicy(
    const Resolver::Result& resolver_result,
    const internal::ClientChannelGlobalParsedConfig* parsed_service_config) {
  // Prefer the LB policy config found in the service config.
  if (parsed_service_config->parsed_lb_config() != nullptr) {
    return parsed_service_config->parsed_lb_config();
  }
  // Try the deprecated LB policy name from the service config.
  // If not, try the setting from channel args.
  absl::optional<absl::string_view> policy_name;
  if (!parsed_service_config->parsed_deprecated_lb_policy().empty()) {
    policy_name = parsed_service_config->parsed_deprecated_lb_policy();
  } else {
    policy_name = resolver_result.args.GetString(GRPC_ARG_LB_POLICY_NAME);
    bool requires_config = false;
    if (policy_name.has_value() &&
        (!CoreConfiguration::Get()
              .lb_policy_registry()
              .LoadBalancingPolicyExists(*policy_name, &requires_config) ||
         requires_config)) {
      if (requires_config) {
        gpr_log(GPR_ERROR,
                "LB policy: %s passed through channel_args must not "
                "require a config. Using pick_first instead.",
                std::string(*policy_name).c_str());
      } else {
        gpr_log(GPR_ERROR,
                "LB policy: %s passed through channel_args does not exist. "
                "Using pick_first instead.",
                std::string(*policy_name).c_str());
      }
      policy_name = "pick_first";
    }
  }
  // Use pick_first if nothing was specified and we didn't select grpclb
  // above.
  if (!policy_name.has_value()) policy_name = "pick_first";
  // Now that we have the policy name, construct an empty config for it.
  Json config_json = Json::FromArray({Json::FromObject({
      {std::string(*policy_name), Json::FromObject({})},
  })});
  auto lb_policy_config =
      CoreConfiguration::Get().lb_policy_registry().ParseLoadBalancingConfig(
          config_json);
  // The policy name came from one of three places:
  // - The deprecated loadBalancingPolicy field in the service config,
  //   in which case the code in ClientChannelServiceConfigParser
  //   already verified that the policy does not require a config.
  // - One of the hard-coded values here, all of which are known to not
  //   require a config.
  // - A channel arg, in which case we check that the specified policy exists
  //   and accepts an empty config. If not, we revert to using pick_first
  //   lb_policy
  GPR_ASSERT(lb_policy_config.ok());
  return std::move(*lb_policy_config);
}

}  // namespace

void ClientChannel::OnResolverResultChangedLocked(
    Resolver::Result result) {
  // Handle race conditions.
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: got resolver result", this);
  }
  // Grab resolver result health callback.
  auto resolver_callback = std::move(result.result_health_callback);
  absl::Status resolver_result_status;
  // We only want to trace the address resolution in the follow cases:
  // (a) Address resolution resulted in service config change.
  // (b) Address resolution that causes number of backends to go from
  //     zero to non-zero.
  // (c) Address resolution that causes number of backends to go from
  //     non-zero to zero.
  // (d) Address resolution that causes a new LB policy to be created.
  //
  // We track a list of strings to eventually be concatenated and traced.
  std::vector<const char*> trace_strings;
  const bool resolution_contains_addresses =
      result.addresses.ok() && !result.addresses->empty();
  if (!resolution_contains_addresses &&
      previous_resolution_contained_addresses_) {
    trace_strings.push_back("Address list became empty");
  } else if (resolution_contains_addresses &&
             !previous_resolution_contained_addresses_) {
    trace_strings.push_back("Address list became non-empty");
  }
  previous_resolution_contained_addresses_ = resolution_contains_addresses;
  std::string service_config_error_string_storage;
  if (!result.service_config.ok()) {
    service_config_error_string_storage =
        result.service_config.status().ToString();
    trace_strings.push_back(service_config_error_string_storage.c_str());
  }
  // Choose the service config.
  RefCountedPtr<ServiceConfig> service_config;
  RefCountedPtr<ConfigSelector> config_selector;
  if (!result.service_config.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: resolver returned service config error: %s",
              this, result.service_config.status().ToString().c_str());
    }
    // If the service config was invalid, then fallback to the
    // previously returned service config.
    if (saved_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "client_channel=%p: resolver returned invalid service config. "
                "Continuing to use previous service config.",
                this);
      }
      service_config = saved_service_config_;
      config_selector = saved_config_selector_;
    } else {
      // We received a service config error and we don't have a
      // previous service config to fall back to.  Put the channel into
      // TRANSIENT_FAILURE.
      OnResolverErrorLocked(result.service_config.status());
      trace_strings.push_back("no valid service config");
      resolver_result_status =
          absl::UnavailableError("no valid service config");
    }
  } else if (*result.service_config == nullptr) {
    // Resolver did not return any service config.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO,
              "client_channel=%p: resolver returned no service config. Using default "
              "service config for channel.",
              this);
    }
    service_config = default_service_config_;
  } else {
    // Use ServiceConfig and ConfigSelector returned by resolver.
    service_config = std::move(*result.service_config);
    config_selector = result.args.GetObjectRef<ConfigSelector>();
  }
  // Note: The only case in which service_config is null here is if the resolver
  // returned a service config error and we don't have a previous service
  // config to fall back to.
  if (service_config != nullptr) {
    // Extract global config for client channel.
    const internal::ClientChannelGlobalParsedConfig* parsed_service_config =
        static_cast<const internal::ClientChannelGlobalParsedConfig*>(
            service_config->GetGlobalParsedConfig(
                service_config_parser_index_));
    // Choose LB policy config.
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config =
        ChooseLbPolicy(result, parsed_service_config);
    // Check if the ServiceConfig has changed.
    const bool service_config_changed =
        saved_service_config_ == nullptr ||
        service_config->json_string() != saved_service_config_->json_string();
    // Check if the ConfigSelector has changed.
    const bool config_selector_changed = !ConfigSelector::Equals(
        saved_config_selector_.get(), config_selector.get());
    // If either has changed, apply the global parameters now.
    if (service_config_changed || config_selector_changed) {
      // Update service config in control plane.
      UpdateServiceConfigInControlPlaneLocked(
          std::move(service_config), std::move(config_selector),
          std::string(lb_policy_config->name()));
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: service config not changed", this);
    }
    // Create or update LB policy, as needed.
    resolver_result_status = CreateOrUpdateLbPolicyLocked(
        std::move(lb_policy_config),
        parsed_service_config->health_check_service_name(), std::move(result));
    if (service_config_changed || config_selector_changed) {
      // Start using new service config for calls.
      // This needs to happen after the LB policy has been updated, since
      // the ConfigSelector may need the LB policy to know about new
      // destinations before it can send RPCs to those destinations.
      UpdateServiceConfigInDataPlaneLocked();
      // TODO(ncteisen): might be worth somehow including a snippet of the
      // config in the trace, at the risk of bloating the trace logs.
      trace_strings.push_back("Service config changed");
    }
  }
  // Invoke resolver callback if needed.
  if (resolver_callback != nullptr) {
    resolver_callback(std::move(resolver_result_status));
  }
  // Add channel trace event.
  if (!trace_strings.empty()) {
    std::string message =
        absl::StrCat("Resolution event: ", absl::StrJoin(trace_strings, ", "));
    if (channelz_node_ != nullptr) {
      channelz_node_->AddTraceEvent(channelz::ChannelTrace::Severity::Info,
                                    grpc_slice_from_cpp_string(message));
    }
  }
}

void ClientChannel::OnResolverErrorLocked(absl::Status status) {
  if (resolver_ == nullptr) return;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: resolver transient failure: %s", this,
            status.ToString().c_str());
  }
  // If we already have an LB policy from a previous resolution
  // result, then we continue to let it set the connectivity state.
  // Otherwise, we go into TRANSIENT_FAILURE.
  if (lb_policy_ == nullptr) {
    // Update connectivity state.
    UpdateStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE, status,
                      "resolver failure");
    {
      MutexLock lock(&resolution_mu_);
      // Update resolver transient failure.
      resolver_transient_failure_error_ =
          MaybeRewriteIllegalStatusCode(status, "resolver");
      ReprocessQueuedResolverCalls();
    }
  }
}

absl::Status ClientChannel::CreateOrUpdateLbPolicyLocked(
    RefCountedPtr<LoadBalancingPolicy::Config> lb_policy_config,
    const absl::optional<std::string>& health_check_service_name,
    Resolver::Result result) {
  // Construct update.
  LoadBalancingPolicy::UpdateArgs update_args;
  if (!result.addresses.ok()) {
    update_args.addresses = result.addresses.status();
  } else {
    update_args.addresses = std::make_shared<EndpointAddressesListIterator>(
        std::move(*result.addresses));
  }
  update_args.config = std::move(lb_policy_config);
  update_args.resolution_note = std::move(result.resolution_note);
  // Remove the config selector from channel args so that we're not holding
  // unnecessary refs that cause it to be destroyed somewhere other than in the
  // WorkSerializer.
  update_args.args = result.args.Remove(GRPC_ARG_CONFIG_SELECTOR);
  // Add health check service name to channel args.
  if (health_check_service_name.has_value()) {
    update_args.args = update_args.args.Set(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME,
                                            *health_check_service_name);
  }
  // Create policy if needed.
  if (lb_policy_ == nullptr) {
    lb_policy_ = CreateLbPolicyLocked(update_args.args);
  }
  // Update the policy.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: Updating child policy %p", this,
            lb_policy_.get());
  }
  return lb_policy_->UpdateLocked(std::move(update_args));
}

// Creates a new LB policy.
OrphanablePtr<LoadBalancingPolicy> ClientChannel::CreateLbPolicyLocked(
    const ChannelArgs& args) {
  // The LB policy will start in state CONNECTING but will not
  // necessarily send us an update synchronously, so set state to
  // CONNECTING (in case the resolver had previously failed and put the
  // channel into TRANSIENT_FAILURE) and make sure we have a queueing picker.
  UpdateStateAndPickerLocked(
      GRPC_CHANNEL_CONNECTING, absl::Status(), "started resolving",
      MakeRefCounted<LoadBalancingPolicy::QueuePicker>(nullptr));
  // Now create the LB policy.
  LoadBalancingPolicy::Args lb_policy_args;
  lb_policy_args.work_serializer = work_serializer_;
  lb_policy_args.channel_control_helper =
      std::make_unique<ClientChannelControlHelper>(this);
  lb_policy_args.args = args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_client_channel_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: created new LB policy %p", this,
            lb_policy.get());
  }
  grpc_pollset_set_add_pollset_set(lb_policy->interested_parties(),
                                   interested_parties_);
  return lb_policy;
}

void ClientChannel::UpdateServiceConfigInControlPlaneLocked(
    RefCountedPtr<ServiceConfig> service_config,
    RefCountedPtr<ConfigSelector> config_selector, std::string lb_policy_name) {
  std::string service_config_json(service_config->json_string());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: using service config: \"%s\"", this,
            service_config_json.c_str());
  }
  // Save service config.
  saved_service_config_ = std::move(service_config);
  // Swap out the data used by GetChannelInfo().
  {
    MutexLock lock(&info_mu_);
    info_lb_policy_name_ = std::move(lb_policy_name);
    info_service_config_json_ = std::move(service_config_json);
  }
  // Save config selector.
  saved_config_selector_ = std::move(config_selector);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: using ConfigSelector %p", this,
            saved_config_selector_.get());
  }
}

void ClientChannel::UpdateServiceConfigInDataPlaneLocked() {
  // Grab ref to service config.
  RefCountedPtr<ServiceConfig> service_config = saved_service_config_;
  // Grab ref to config selector.  Use default if resolver didn't supply one.
  RefCountedPtr<ConfigSelector> config_selector = saved_config_selector_;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: switching to ConfigSelector %p", this,
            saved_config_selector_.get());
  }
  if (config_selector == nullptr) {
    config_selector =
        MakeRefCounted<DefaultConfigSelector>(saved_service_config_);
  }
  ChannelArgs new_args =
      channel_args_.SetObject(this).SetObject(service_config);
  bool enable_retries =
      !new_args.WantMinimalStack() &&
      new_args.GetBool(GRPC_ARG_ENABLE_RETRIES).value_or(true);
  // Construct dynamic filter stack.
  std::vector<const grpc_channel_filter*> filters =
      config_selector->GetFilters();
  if (enable_retries) {
    filters.push_back(&RetryFilter::kVtable);
  } else {
    filters.push_back(&DynamicTerminationFilter::kFilterVtable);
  }
  RefCountedPtr<DynamicFilters> dynamic_filters =
      DynamicFilters::Create(new_args, std::move(filters));
  GPR_ASSERT(dynamic_filters != nullptr);
  // Grab data plane lock to update service config.
  //
  // We defer unreffing the old values (and deallocating memory) until
  // after releasing the lock to keep the critical section small.
  {
    MutexLock lock(&resolution_mu_);
    resolver_transient_failure_error_ = absl::OkStatus();
    // Update service config.
    received_service_config_data_ = true;
    // Old values will be unreffed after lock is released.
    service_config_.swap(service_config);
    config_selector_.swap(config_selector);
    dynamic_filters_.swap(dynamic_filters);
    // Re-process queued calls asynchronously.
    ReprocessQueuedResolverCalls();
  }
  // Old values will be unreffed after lock is released when they go out
  // of scope.
}

void ClientChannel::CreateResolverLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: starting name resolution for %s", this,
            uri_to_resolve_.c_str());
  }
  resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      uri_to_resolve_, channel_args_, interested_parties_, work_serializer_,
      std::make_unique<ResolverResultHandler>(this));
  // Since the validity of the args was checked when the channel was created,
  // CreateResolver() must return a non-null result.
  GPR_ASSERT(resolver_ != nullptr);
  UpdateStateLocked(GRPC_CHANNEL_CONNECTING, absl::Status(),
                    "started resolving");
  resolver_->StartLocked();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: created resolver=%p", this, resolver_.get());
  }
}

void ClientChannel::DestroyResolverAndLbPolicyLocked() {
  if (resolver_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: shutting down resolver=%p", this,
              resolver_.get());
    }
    resolver_.reset();
    // Clear resolution state.
    saved_service_config_.reset();
    saved_config_selector_.reset();
    // Acquire resolution lock to update config selector and associated state.
    // To minimize lock contention, we wait to unref these objects until
    // after we release the lock.
    RefCountedPtr<ServiceConfig> service_config_to_unref;
    RefCountedPtr<ConfigSelector> config_selector_to_unref;
    RefCountedPtr<DynamicFilters> dynamic_filters_to_unref;
    {
      MutexLock lock(&resolution_mu_);
      received_service_config_data_ = false;
      service_config_to_unref = std::move(service_config_);
      config_selector_to_unref = std::move(config_selector_);
      dynamic_filters_to_unref = std::move(dynamic_filters_);
    }
    // Clear LB policy if set.
    if (lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO, "client_channel=%p: shutting down lb_policy=%p", this,
                lb_policy_.get());
      }
      grpc_pollset_set_del_pollset_set(lb_policy_->interested_parties(),
                                       interested_parties_);
      lb_policy_.reset();
    }
  }
}

void ClientChannel::UpdateStateLocked(grpc_connectivity_state state,
                                            const absl::Status& status,
                                            const char* reason) {
  if (state != GRPC_CHANNEL_SHUTDOWN &&
      state_tracker_.state() == GRPC_CHANNEL_SHUTDOWN) {
    Crash("Illegal transition SHUTDOWN -> anything");
  }
  state_tracker_.SetState(state, status, reason);
  if (channelz_node_ != nullptr) {
    channelz_node_->SetConnectivityState(state);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            channelz::ChannelNode::GetChannelConnectivityStateChangeString(
                state)));
  }
}

void ClientChannel::UpdateStateAndPickerLocked(
    grpc_connectivity_state state, const absl::Status& status,
    const char* reason,
    RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) {
  UpdateStateLocked(state, status, reason);
  // Grab the LB lock to update the picker and trigger reprocessing of the
  // queued picks.
  // Old picker will be unreffed after releasing the lock.
  MutexLock lock(&lb_mu_);
  picker_.swap(picker);
  // Reprocess queued picks.
  for (auto& call : lb_queued_calls_) {
    call->RemoveCallFromLbQueuedCallsLocked();
    call->RetryPickLocked();
  }
  lb_queued_calls_.clear();
}

namespace {

// TODO(roth): Remove this in favor of the gprpp Match() function once
// we can do that without breaking lock annotations.
template <typename T>
T HandlePickResult(
    LoadBalancingPolicy::PickResult* result,
    std::function<T(LoadBalancingPolicy::PickResult::Complete*)> complete_func,
    std::function<T(LoadBalancingPolicy::PickResult::Queue*)> queue_func,
    std::function<T(LoadBalancingPolicy::PickResult::Fail*)> fail_func,
    std::function<T(LoadBalancingPolicy::PickResult::Drop*)> drop_func) {
  auto* complete_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Complete>(&result->result);
  if (complete_pick != nullptr) {
    return complete_func(complete_pick);
  }
  auto* queue_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Queue>(&result->result);
  if (queue_pick != nullptr) {
    return queue_func(queue_pick);
  }
  auto* fail_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Fail>(&result->result);
  if (fail_pick != nullptr) {
    return fail_func(fail_pick);
  }
  auto* drop_pick =
      absl::get_if<LoadBalancingPolicy::PickResult::Drop>(&result->result);
  GPR_ASSERT(drop_pick != nullptr);
  return drop_func(drop_pick);
}

}  // namespace

grpc_error_handle ClientChannel::DoPingLocked(grpc_transport_op* op) {
  if (state_tracker_.state() != GRPC_CHANNEL_READY) {
    return GRPC_ERROR_CREATE("channel not connected");
  }
  LoadBalancingPolicy::PickResult result;
  {
    MutexLock lock(&lb_mu_);
    result = picker_->Pick(LoadBalancingPolicy::PickArgs());
  }
  return HandlePickResult<grpc_error_handle>(
      &result,
      // Complete pick.
      [op](LoadBalancingPolicy::PickResult::Complete* complete_pick)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(
              *ClientChannel::work_serializer_) {
            SubchannelWrapper* subchannel = static_cast<SubchannelWrapper*>(
                complete_pick->subchannel.get());
            RefCountedPtr<ConnectedSubchannel> connected_subchannel =
                subchannel->connected_subchannel();
            if (connected_subchannel == nullptr) {
              return GRPC_ERROR_CREATE("LB pick for ping not connected");
            }
            connected_subchannel->Ping(op->send_ping.on_initiate,
                                       op->send_ping.on_ack);
            return absl::OkStatus();
          },
      // Queue pick.
      [](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        return GRPC_ERROR_CREATE("LB picker queued call");
      },
      // Fail pick.
      [](LoadBalancingPolicy::PickResult::Fail* fail_pick) {
        return absl_status_to_grpc_error(fail_pick->status);
      },
      // Drop pick.
      [](LoadBalancingPolicy::PickResult::Drop* drop_pick) {
        return absl_status_to_grpc_error(drop_pick->status);
      });
}

void ClientChannel::StartTransportOpLocked(grpc_transport_op* op) {
  // Connectivity watch.
  if (op->start_connectivity_watch != nullptr) {
    state_tracker_.AddWatcher(op->start_connectivity_watch_state,
                              std::move(op->start_connectivity_watch));
  }
  if (op->stop_connectivity_watch != nullptr) {
    state_tracker_.RemoveWatcher(op->stop_connectivity_watch);
  }
  // Ping.
  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    grpc_error_handle error = DoPingLocked(op);
    if (!error.ok()) {
      ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_initiate, error);
      ExecCtx::Run(DEBUG_LOCATION, op->send_ping.on_ack, error);
    }
    op->bind_pollset = nullptr;
    op->send_ping.on_initiate = nullptr;
    op->send_ping.on_ack = nullptr;
  }
  // Reset backoff.
  if (op->reset_connect_backoff) {
    if (lb_policy_ != nullptr) {
      lb_policy_->ResetBackoffLocked();
    }
  }
  // Disconnect or enter IDLE.
  if (!op->disconnect_with_error.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: disconnect_with_error: %s", this,
              StatusToString(op->disconnect_with_error).c_str());
    }
    DestroyResolverAndLbPolicyLocked();
    intptr_t value;
    if (grpc_error_get_int(op->disconnect_with_error,
                           StatusIntProperty::ChannelConnectivityState,
                           &value) &&
        static_cast<grpc_connectivity_state>(value) == GRPC_CHANNEL_IDLE) {
      if (disconnect_error_.ok()) {  // Ignore if we're shutting down.
        // Enter IDLE state.
        UpdateStateAndPickerLocked(GRPC_CHANNEL_IDLE, absl::Status(),
                                   "channel entering IDLE", nullptr);
        // TODO(roth): Do we need to check for any queued picks here, in
        // case there's a race condition in the client_idle filter?
        // And maybe also check for calls in the resolver queue?
      }
    } else {
      // Disconnect.
      GPR_ASSERT(disconnect_error_.ok());
      disconnect_error_ = op->disconnect_with_error;
      UpdateStateAndPickerLocked(
          GRPC_CHANNEL_SHUTDOWN, absl::Status(), "shutdown from API",
          MakeRefCounted<LoadBalancingPolicy::TransientFailurePicker>(
              grpc_error_to_absl_status(op->disconnect_with_error)));
      // TODO(roth): If this happens when we're still waiting for a
      // resolver result, we need to trigger failures for all calls in
      // the resolver queue here.
    }
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "start_transport_op");
  ExecCtx::Run(DEBUG_LOCATION, op->on_consumed, absl::OkStatus());
}

void ClientChannel::GetChannelInfo(grpc_channel_element* elem,
                                         const grpc_channel_info* info) {
  auto* client_channel = static_cast<ClientChannel*>(elem->channel_data);
  MutexLock lock(&client_channel->info_mu_);
  if (info->lb_policy_name != nullptr) {
    *info->lb_policy_name = gpr_strdup(client_channel->info_lb_policy_name_.c_str());
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json =
        gpr_strdup(client_channel->info_service_config_json_.c_str());
  }
}

void ClientChannel::TryToConnectLocked() {
  if (disconnect_error_.ok()) {
    if (lb_policy_ != nullptr) {
      lb_policy_->ExitIdleLocked();
    } else if (resolver_ == nullptr) {
      CreateResolverLocked();
    }
  }
  GRPC_CHANNEL_STACK_UNREF(owning_stack_, "TryToConnect");
}

grpc_connectivity_state ClientChannel::CheckConnectivityState(
    bool try_to_connect) {
  // state_tracker_ is guarded by work_serializer_, which we're not
  // holding here.  But the one method of state_tracker_ that *is*
  // thread-safe to call without external synchronization is the state()
  // method, so we can disable thread-safety analysis for this one read.
  grpc_connectivity_state out = ABSL_TS_UNCHECKED_READ(state_tracker_).state();
  if (out == GRPC_CHANNEL_IDLE && try_to_connect) {
    GRPC_CHANNEL_STACK_REF(owning_stack_, "TryToConnect");
    work_serializer_->Run([this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                              *work_serializer_) { TryToConnectLocked(); },
                          DEBUG_LOCATION);
  }
  return out;
}

void ClientChannel::AddConnectivityWatcher(
    grpc_connectivity_state initial_state,
    OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) {
  work_serializer_->Run(
      [self = RefAsSubclass<ClientChannel>(), initial_state,
       watcher = std::move(watcher)]() {
        self->state_tracker_.AddWatcher(initial_state, std::move(watcher));
      },
      DEBUG_LOCATION);
}

void ClientChannel::RemoveConnectivityWatcher(
    AsyncConnectivityStateWatcherInterface* watcher) {
  work_serializer_->Run([self = RefAsSubclass<ClientChannel>(), watcher]() {
                          self->state_tracker_.RemoveWatcher(watcher);
                        },
                        DEBUG_LOCATION);
}

namespace {

ClientChannelServiceConfigCallData* GetServiceConfigCallDataFromContext() {
  auto* legacy_context = GetContext<grpc_call_context_element>();
  return static_cast<ClientChannelServiceConfigCallData*>(
      legacy_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
}

//
// NoRetryCallDestination
//

class NoRetryCallDestination : public CallDestination {
 public:
  explicit NoRetryCallDestination(
      RefCountedPtr<ClientChannel> client_channel)
      : client_channel_(std::move(client_channel)) {}

  void StartCall(CallHandler call_handler) override {
    call_handler.SpawnGuarded(
        "drain_send_initial_metadata",
        [call_handler = std::move(call_handler)]() {
          // Wait to get client initial metadata from the call handler.
          return Map(
              call_handler.PullClientInitialMetadata(),
              [call_handler](
                  ClientMetadataHandle client_initial_metadata) mutable {
                // Create the LoadBalancedCall.
                CallInitiator call_initiator =
                    client_channel_->CreateLoadBalancedCall(
                        std::move(client_initial_metadata),
                        /*on_commit=*/[]() {
                          auto* service_config_call_data =
                              GetServiceConfigCallDataFromContext();
                          service_config_call_data->Commit();
                        },
                        /*is_transparent_retry=*/false);
                // Propagate operations from the parent call's handler to
                // the LoadBalancedCall's initiator.
                ForwardCall(std::move(call_handler),
                            std::move(call_initiator));
              });
        });
  }

  void Orphan() override { delete this; }

 private:
  RefCountedPtr<ClientChannel> client_channel_;
};

}  // namespace

absl::Status ClientChannel::ApplyServiceConfigToCall(
    ConfigSelector& config_selector,
    ClientMetadataHandle& client_initial_metadata) const {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: applying service config to call",
            this);
  }
  // Create a ClientChannelServiceConfigCallData for the call.  This stores
  // a ref to the ServiceConfig and caches the right set of parsed configs
  // to use for the call.  The ClientChannelServiceConfigCallData will store
  // itself in the call context, so that it can be accessed by filters
  // below us in the stack, and it will be cleaned up when the call ends.
  auto* service_config_call_data =
      GetContext<Arena>()->New<ClientChannelServiceConfigCallData>(
          GetContext<Arena>(), GetContext<grpc_call_context_element>());
  // Use the ConfigSelector to determine the config for the call.
  absl::Status call_config_status = config_selector.GetCallConfig(
      {client_initial_metadata.get(), GetContext<Arena>(),
       service_config_call_data});
  if (!call_config_status.ok()) {
    return MaybeRewriteIllegalStatusCode(call_config_status, "ConfigSelector");
  }
  // Apply our own method params to the call.
  auto* method_params = static_cast<ClientChannelMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          service_config_parser_index_));
  if (method_params != nullptr) {
    // If the service config specifies a deadline, update the call's
    // deadline timer.
    if (method_params->timeout() != Duration::Zero()) {
      CallContext* call_context = GetContext<CallContext>();
      const Timestamp per_method_deadline =
          Timestamp::FromCycleCounterRoundUp(call_context->call_start_time()) +
          method_params->timeout();
      call_context->UpdateDeadline(per_method_deadline);
    }
    // If the service config set wait_for_ready and the application
    // did not explicitly set it, use the value from the service config.
    auto* wait_for_ready =
        client_initial_metadata->GetOrCreatePointer(WaitForReady());
    if (method_params->wait_for_ready().has_value() &&
        !wait_for_ready->explicitly_set) {
      wait_for_ready->value = method_params->wait_for_ready().value();
    }
  }
  return absl::OkStatus();
}

CallInitiator ClientChannel::CreateCall(
    ClientMetadataHandle client_initial_metadata, Arena* arena) {
  // Exit IDLE if needed.
  CheckConnectivityState(/*try_to_connect=*/true);
  // Create an initiator/handler pair.
  auto call = MakeCall(GetContext<EventEngine>(), arena);
  // Spawn a promise to wait for the resolver result.
  // This will eventually start using the handler, which will allow the
  // initiator to make progress.
  call.initiator.SpawnGuarded(
      "wait-for-name-resolution",
      [self = RefAsSubclass<ClientChannel>(),
       client_initial_metadata = std::move(client_initial_metadata),
       initiator = call.initiator,
       handler = std::move(call.handler), was_queued = false]() mutable {
        const bool wait_for_ready =
            client_initial_metadata->GetOrCreatePointer(WaitForReady())->value;
        return Map(
            // Wait for the resolver result.
            resolver_data_for_calls_.NextWhen(
                [wait_for_ready, &was_queued](
                    const absl::StatusOr<ResolverDataForCalls> result) {
                  bool got_result = false;
                  // If the resolver reports an error but the call is
                  // wait_for_ready, keep waiting for the next result
                  // instead of failing the call.
                  if (!result.ok()) {
                    got_result = !wait_for_ready;
                  } else {
                    // Not an error.  Make sure we actually have a result.
                    got_result = *result != nullptr;
                  }
                  if (!got_result) was_queued = true;
                  return got_result;
                }),
                // Handle resolver result.
                [self, &was_queued, &initiator, &handler,
                 client_initial_metadata = std::move(client_initial_metadata)](
                    ResolverDataForCalls resolver_data) mutable {
                  // Apply service config to call.
                  absl::Status status = ApplyServiceConfigToCall(
                      *resolver_data.config_selector, client_initial_metadata);
                  if (!status.ok()) return status;
                  // If the call was queued, add trace annotation.
                  if (was_queued) {
                    auto* legacy_context =
                        GetContext<grpc_call_context_element>();
                    auto* call_tracer =
                        static_cast<CallTracerAnnotationInterface*>(
                            legacy_context[
                                GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE]
                            .value);
                    if (call_tracer != nullptr) {
                      call_tracer->RecordAnnotation(
                          "Delayed name resolution complete.");
                    }
                  }
                  // Now inject initial metadata into the call.
                  initiator.SpawnGuarded(
                      "send_initial_metadata",
                      [initiator, client_initial_metadata =
                           std::move(client_initial_metadata)]() mutable {
                        return initiator.PushClientInitialMetadata(
                            std::move(client_initial_metadata));
                      });
                  // Finish constructing the call with the right filter
                  // stack and destination.
                  handler.SetStack(std::move(resolver_data.filter_stack));
                  self->call_destination_->StartCall(std::move(handler));
                  return absl::OkStatus();
                });
      });
  // Return the initiator.
  return call.initiator;
}

namespace {

class LbMetadata : public LoadBalancingPolicy::MetadataInterface {
 public:
  explicit LbMetadata(grpc_metadata_batch* batch) : batch_(batch) {}

  void Add(absl::string_view key, absl::string_view value) override {
    if (batch_ == nullptr) return;
    // Gross, egregious hack to support legacy grpclb behavior.
    // TODO(ctiller): Use a promise context for this once that plumbing is done.
    if (key == GrpcLbClientStatsMetadata::key()) {
      batch_->Set(
          GrpcLbClientStatsMetadata(),
          const_cast<GrpcLbClientStats*>(
              reinterpret_cast<const GrpcLbClientStats*>(value.data())));
      return;
    }
    batch_->Append(key, Slice::FromStaticString(value),
                   [key](absl::string_view error, const Slice& value) {
                     gpr_log(GPR_ERROR, "%s",
                             absl::StrCat(error, " key:", key,
                                          " value:", value.as_string_view())
                                 .c_str());
                   });
  }

  std::vector<std::pair<std::string, std::string>> TestOnlyCopyToVector()
      override {
    if (batch_ == nullptr) return {};
    Encoder encoder;
    batch_->Encode(&encoder);
    return encoder.Take();
  }

  absl::optional<absl::string_view> Lookup(absl::string_view key,
                                           std::string* buffer) const override {
    if (batch_ == nullptr) return absl::nullopt;
    return batch_->GetStringValue(key, buffer);
  }

 private:
  class Encoder {
   public:
    void Encode(const Slice& key, const Slice& value) {
      out_.emplace_back(std::string(key.as_string_view()),
                        std::string(value.as_string_view()));
    }

    template <class Which>
    void Encode(Which, const typename Which::ValueType& value) {
      auto value_slice = Which::Encode(value);
      out_.emplace_back(std::string(Which::key()),
                        std::string(value_slice.as_string_view()));
    }

    void Encode(GrpcTimeoutMetadata,
                const typename GrpcTimeoutMetadata::ValueType&) {}
    void Encode(HttpPathMetadata, const Slice&) {}
    void Encode(HttpMethodMetadata,
                const typename HttpMethodMetadata::ValueType&) {}

    std::vector<std::pair<std::string, std::string>> Take() {
      return std::move(out_);
    }

   private:
    std::vector<std::pair<std::string, std::string>> out_;
  };

  grpc_metadata_batch* batch_;
};

void MaybeCreateCallAttemptTracer(bool is_transparent_retry) {
  auto* legacy_context = GetContext<grpc_call_context_element>();
  auto* call_tracer = static_cast<ClientCallTracer*>(
      legacy_context[GRPC_CONTEXT_CALL_TRACER_ANNOTATION_INTERFACE].value);
  if (call_tracer == nullptr) return;
  auto* tracer = call_tracer->StartNewAttempt(is_transparent_retry);
  legacy_context[GRPC_CONTEXT_CALL_TRACER].value = tracer;
}

ClientCallTracer::CallAttemptTracer* GetCallAttemptTracerFromContext() {
  auto* legacy_context = GetContext<grpc_call_context_element>();
  return static_cast<ClientCallTracer::CallAttemptTracer*>(
      legacy_context[GRPC_CONTEXT_CALL_TRACER].value);
}

// Context type for subchannel call tracker.
template <>
struct ContextType<LoadBalancingPolicy::SubchannelCallTracker*> {};

// A filter to handle updating with the call tracer and LB subchannel
// call tracker inside the LB call.
class LbCallTracingFilter : public ImplementChannelFilter<LbCallTracingFilter> {
 public:
  static absl::StatusOr<LbCallTracingFilter> Create(const ChannelArgs&,
                                                    ChannelFilter::Args) {
    return LbCallTracingFilter();
  }

  class Call {
   public:
    void OnClientInitialMetadata(ClientMetadata& metadata) {
      auto* tracer = GetCallAttemptTracerFromContext();
      if (tracer == nullptr) return;
      tracer->RecordSendInitialMetadata(metadata.get());
    }

    void OnServerInitialMetadata(ServerMetadata& metadata) {
      auto* tracer = GetCallAttemptTracerFromContext();
      if (tracer == nullptr) return;
      tracer->RecordReceivedInitialMetadata(metadata.get());
      // Save peer string for later use.
      Slice* peer_string = metadata->get_pointer(PeerString());
      if (peer_string != nullptr) peer_string_ = peer_string->Ref();
    }

    static const NoInterceptor OnClientToServerMessage;
    static const NoInterceptor OnServerToClientMessage;

    void OnClientToServerMessagesClosed() {
      auto* tracer = GetCallAttemptTracerFromContext();
      if (tracer == nullptr) return;
      // TODO(roth): Change CallTracer API to not pass metadata
      // batch to this method, since the batch is always empty.
      grpc_metadata_batch metadata(GetContext<Arena>());
      tracer->RecordSendTrailingMetadata(&metadata);
    }

    void OnServerTrailingMetadata(ServerMetadata& metadata) {
      auto* tracer = GetCallAttemptTracerFromContext();
      auto* call_tracker =
          GetContext<LoadBalancingPolicy::SubchannelCallTracker*>();
      absl::Status status;
      if (tracer != nullptr || call_tracker != nullptr) {
        grpc_status_code code = metadata->get(GrpcStatusMetadata())
                                    .value_or(GRPC_STATUS_UNKNOWN);
        if (code != GRPC_STATUS_OK) {
          absl::string_view message;
          if (const auto* grpc_message =
                  metadata->get_pointer(GrpcMessageMetadata())) {
            message = grpc_message->as_string_view();
          }
          status =
              absl::Status(static_cast<absl::StatusCode>(code), message);
        }
      }
      if (tracer != nullptr) {
        tracer->RecordReceivedTrailingMetadata(
            status, metadata.get(),
            &GetContext<CallContext>()->call_stats()->transport_stream_stats,
            peer_string_.as_string_view());
      }
      if (call_tracker != nullptr) {
        LbMetadata metadata(metadata.get());
        BackendMetricAccessor backend_metric_accessor(metadata.get());
        LoadBalancingPolicy::SubchannelCallTrackerInterface::FinishArgs args = {
            peer_string_.as_string_view(), status, metadata.get(),
            &backend_metric_accessor};
        call_tracker->Finish(args);
        delete call_tracker;
      }
    }

    void OnFinalize(const grpc_call_final_info*) {
      auto* tracer = GetCallAttemptTracerFromContext();
      if (tracer == nullptr) return;
      gpr_timespec latency =
          gpr_cycle_counter_sub(gpr_get_cycle_counter(), lb_call_start_time_);
      tracer->RecordEnd(latency);
    }

   private:
    // Interface for accessing backend metric data in the LB call tracker.
    class BackendMetricAccessor
        : public LoadBalancingPolicy::BackendMetricAccessor {
     public:
      explicit BackendMetricAccessor(
          grpc_metadata_batch* server_trailing_metadata)
          : server_trailing_metadata_(server_trailing_metadata) {}

      ~BackendMetricAccessor() override {
        if (backend_metric_data_ != nullptr) {
          backend_metric_data_->BackendMetricData::~BackendMetricData();
        }
      }

      const BackendMetricData* GetBackendMetricData() override {
        if (backend_metric_data_ == nullptr) {
          if (const auto* md = recv_trailing_metadata_->get_pointer(
                  EndpointLoadMetricsBinMetadata())) {
            BackendMetricAllocator allocator;
            backend_metric_data_ =
                ParseBackendMetricData(md->as_string_view(), &allocator);
          }
        }
        return backend_metric_data_;
      }

     private:
      class BackendMetricAllocator : public BackendMetricAllocatorInterface {
       public:
        BackendMetricData* AllocateBackendMetricData() override {
          return GetContext<Arena>()->New<BackendMetricData>();
        }

        char* AllocateString(size_t size) override {
          return static_cast<char*>(GetContext<Arena>()->Alloc(size));
        }
      };

      grpc_metadata_batch* send_trailing_metadata_;
      const BackendMetricData* backend_metric_data_ = nullptr;
    };

    Slice peer_string_;
  };
};

}  // namespace

CallInitiator ClientChannel::CreateLoadBalancedCall(
    ClientMetadataHandle client_initial_metadata,
    absl::AnyInvocable<void()> on_commit, bool is_transparent_retry) {
  // If there is a call tracer, create a call attempt tracer.
  MaybeCreateCallAttemptTracer(is_transparent_retry);
  // Create an arena.
  const size_t initial_size = lb_call_size_estimator_.CallSizeEstimate();
// FIXME: do we want to do this for LB calls, or do we want a separate stat for this?
  //global_stats().IncrementCallInitialSize(initial_size);
  Arena* arena = Arena::Create(initial_size, &lb_call_allocator_);
  // Create an initiator/handler pair using the arena.
// FIXME: pass in a callback that the CallSpine will use to destroy the arena:
// [](Arena* arena) {
//   lb_call_size_estimator_.UpdateCallSizeEstimate(arena->TotalUsedBytes());
//   arena->Destroy();
// }
  auto call = MakeCall(GetContext<EventEngine>(), arena);
  // Spawn a promise to do the LB pick.
  // This will eventually start using the handler, which will allow the
  // initiator to make progress.
  call.initiator.SpawnGuarded(
      "lb_pick",
      [self = RefAsSubclass<ClientChannel>(),
       client_initial_metadata = std::move(client_initial_metadata),
       initiator = call.initiator, handler = std::move(call.handler),
       on_commit = std::move(on_commit), was_queued = true]() mutable {
        return Map(
            // Wait for the LB picker.
            Loop([last_picker =
                      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>(),
                  client_initial_metadata =
                      std::move(client_initial_metadata),
                  initiator, &was_queued]() mutable {
              return Map(
                  picker_.Next(last_picker),
                  [&last_picker, &client_initial_metadata, &initiator,
                   &was_queued](
                      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>
                      picker) mutable {
                    last_picker = std::move(picker);
                    // Returns 3 possible things:
                    // - Continue to queue the pick
                    // - non-OK status to fail the pick
                    // - a connected subchannel to complete the pick
                    auto result = PickSubchannel(
                        *last_picker, client_initial_metadata, initiator);
                    if (result == Continue{}) was_queued = true;
                    return result;
                  });
            }),
            // Create call stack on the connected subchannel.
            [handler = std::move(handler), on_commit = std::move(on_commit),
             &was_queued](
                RefCountedPtr<ConnectedSubchannel> connected_subchannel) {
              // LB pick is done, so indicate that we've committed.
              on_commit();
              // If it was queued, add a trace annotation.
              auto* tracer = GetCallAttemptTracerFromContext();
              if (was_queued && tracer != nullptr) {
                tracer->RecordAnnotation("Delayed LB pick complete.");
              }
              // Build call stack.
// FIXME: need to insert LbCallTracingFilter at the top of the stack
              handler.SetStack(connected_subchannel->GetStack());
              connected_subchannel->StartCall(std::move(handler));
            });
      });
  // Return the initiator.
  return call.initiator;
}

namespace {

class LbCallState : public ClientChannelLbCallState {
 public:
  void* Alloc(size_t size) override { return GetContext<Arena>()->Alloc(size); }

  // Internal API to allow first-party LB policies to access per-call
  // attributes set by the ConfigSelector.
  ServiceConfigCallData::CallAttributeInterface* GetCallAttribute(
      UniqueTypeName type) const override {
    auto* service_config_call_data = GetServiceConfigCallDataFromContext();
    return service_config_call_data->GetCallAttribute(type);
  }

  ClientCallTracer::CallAttemptTracer* GetCallAttemptTracer() const override {
    auto* legacy_context = GetContext<grpc_call_context_element>();
    return static_cast<ClientCallAttemptTracer*>(
        legacy_context[GRPC_CONTEXT_CALL_TRACER].value);
  }
};

}  // namespace

LoopCtl<absl::StatusOr<RefCountedPtr<ConnectedSubchannel>>>
ClientChannel::PickSubchannel(LoadBalancingPolicy::SubchannelPicker& picker,
                              ClientMetadataHandle& client_initial_metadata,
                              CallInitiator& call_initiator) {
  // Perform LB pick.
  LoadBalancingPolicy::PickArgs pick_args;
  Slice* path = client_initial_metadata->get_pointer(HttpPathMetadata());
  GPR_ASSERT(path != nullptr);
  pick_args.path = path->as_string_view();
  LbCallState lb_call_state;
  pick_args.call_state = &lb_call_state;
  LbMetadata initial_metadata(client_initial_metadata.get());
  pick_args.initial_metadata = &initial_metadata;
  auto result = picker.Pick(pick_args);
  // Handle result.
  return HandlePickResult<bool>(
      &result,
      // CompletePick
      [&](LoadBalancingPolicy::PickResult::Complete* complete_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO,
                  "client_channel=%p lb_call=%p: LB pick succeeded: subchannel=%p",
                  client_channel_, this, complete_pick->subchannel.get());
        }
        GPR_ASSERT(complete_pick->subchannel != nullptr);
        // Grab a ref to the connected subchannel while we're still
        // holding the data plane mutex.
        SubchannelWrapper* subchannel =
            static_cast<SubchannelWrapper*>(complete_pick->subchannel.get());
        auto connected_subchannel = subchannel->connected_subchannel();
        // If the subchannel has no connected subchannel (e.g., if the
        // subchannel has moved out of state READY but the LB policy hasn't
        // yet seen that change and given us a new picker), then just
        // queue the pick.  We'll try again as soon as we get a new picker.
        if (connected_subchannel == nullptr) {
          if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
            gpr_log(GPR_INFO,
                    "client_channel=%p lb_call=%p: subchannel returned by LB picker "
                    "has no connected subchannel; queueing pick",
                    client_channel_, this);
          }
          return Continue{};
        }
        // If the LB policy returned a call tracker, inform it that the
        // call is starting and add it to context, so that we can notify
        // it when the call finishes.
        if (complete_pick->subchannel_call_tracker != nullptr) {
          complete_pick->subchannel_call_tracker->Start();
          call_initiator.SetContext(
              complete_pick->subchannel_call_tracker.release());
        }
        // Now that we're done with client initial metadata, push it
        // into the call initiator.
        call_initiator.SpawnGuarded(
            "send_initial_metadata",
            [call_initiator, client_initial_metadata =
                 std::move(client_initial_metadata)]() mutable {
              return call_initiator.PushClientInitialMetadata(
                  std::move(client_initial_metadata));
            });
        // Return the connected subchannel.
        return connected_subchannel;
      },
      // QueuePick
      [this](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "client_channel=%p lb_call=%p: LB pick queued", client_channel_,
                  this);
        }
        return Continue{};
      },
      // FailPick
      [this, &error](LoadBalancingPolicy::PickResult::Fail* fail_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "client_channel=%p lb_call=%p: LB pick failed: %s", client_channel_,
                  this, fail_pick->status.ToString().c_str());
        }
        // If wait_for_ready is false, then the error indicates the RPC
        // attempt's final status.
        if (!send_initial_metadata()
                 ->GetOrCreatePointer(WaitForReady())
                 ->value) {
          return MaybeRewriteIllegalStatusCode(std::move(fail_pick->status),
                                               "LB pick");
        }
        // If wait_for_ready is true, then queue to retry when we get a new
        // picker.
        return Continue{};
      },
      // DropPick
      [this, &error](LoadBalancingPolicy::PickResult::Drop* drop_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "client_channel=%p lb_call=%p: LB pick dropped: %s", client_channel_,
                  this, drop_pick->status.ToString().c_str());
        }
        return grpc_error_set_int(
            MaybeRewriteIllegalStatusCode(std::move(drop_pick->status),
                                          "LB drop"),
            StatusIntProperty::kLbPolicyDrop, 1);
      });
}

}  // namespace grpc_core
