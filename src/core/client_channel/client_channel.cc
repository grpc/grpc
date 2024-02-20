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

#include "src/core/client_channel/client_channel.h"

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

#include "src/core/ext/filters/channel_idle/channel_idle_filter.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/client_channel/client_channel_channelz.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/client_channel_service_config.h"
#include "src/core/client_channel/config_selector.h"
#include "src/core/client_channel/dynamic_filters.h"
#include "src/core/client_channel/global_subchannel_pool.h"
#include "src/core/client_channel/local_subchannel_pool.h"
#include "src/core/client_channel/retry_filter.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/client_channel/subchannel_interface_internal.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/promise_based_filter.h"
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
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/load_balancing/backend_metric_parser.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/service_config/service_config_impl.h"

namespace grpc_core {

using grpc_event_engine::experimental::EventEngine;

using internal::ClientChannelMethodParsedConfig;

// Defined in legacy client channel filter.
// TODO(roth): Move these here when we remove the legacy filter.
extern TraceFlag grpc_client_channel_trace;
extern TraceFlag grpc_client_channel_call_trace;
extern TraceFlag grpc_client_channel_lb_call_trace;

//
// ClientChannel::ResolverResultHandler
//

class ClientChannel::ResolverResultHandler : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(
      WeakRefCountedPtr<ClientChannel> client_channel)
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
  WeakRefCountedPtr<ClientChannel> client_channel_;
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
  SubchannelWrapper(WeakRefCountedPtr<ClientChannel> client_channel,
                    RefCountedPtr<Subchannel> subchannel)
      : SubchannelInterface(GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)
                                ? "SubchannelWrapper"
                                : nullptr),
        client_channel_(std::move(client_channel)),
        subchannel_(std::move(subchannel)) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(
          GPR_INFO,
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
              "client_channel=%p: destroying subchannel wrapper %p "
              "for subchannel %p",
              client_channel_.get(), this, subchannel_.get());
    }
  }

  void Orphan() override {
    // Make sure we clean up the channel's subchannel maps inside the
    // WorkSerializer.
    auto self = WeakRefAsSubclass<SubchannelWrapper>(
        DEBUG_LOCATION, "subchannel map cleanup");
    client_channel_->work_serializer_->Run(
        [self = std::move(self)]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                *self->client_channel_->work_serializer_) {
          self->client_channel_->subchannel_wrappers_.erase(self.get());
          if (self->client_channel_->channelz_node_ != nullptr) {
            auto* subchannel_node = self->subchannel_->channelz_node();
            if (subchannel_node != nullptr) {
              auto it = self->client_channel_->subchannel_refcount_map_.find(
                  self->subchannel_.get());
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
      override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
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
                "client_channel=%p: connectivity change for subchannel "
                "wrapper %p subchannel %p; hopping into work_serializer",
                subchannel_wrapper_->client_channel_.get(),
                subchannel_wrapper_.get(),
                subchannel_wrapper_->subchannel_.get());
      }
      subchannel_wrapper_->client_channel_->work_serializer_->Run(
          [self = self.TakeAsSubclass<WatcherWrapper>(), state, status]()
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
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(
            *subchannel_wrapper_->client_channel_->work_serializer_) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "client_channel=%p: processing connectivity change in work "
                "serializer for subchannel wrapper %p subchannel %p watcher=%p "
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
          if (new_keepalive_time >
              subchannel_wrapper_->client_channel_->keepalive_time_) {
            subchannel_wrapper_->client_channel_->keepalive_time_ =
                new_keepalive_time;
            if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
              gpr_log(GPR_INFO,
                      "client_channel=%p: throttling keepalive time to %d",
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
          gpr_log(GPR_ERROR,
                  "client_channel=%p: Illegal keepalive throttling value %s",
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

  WeakRefCountedPtr<ClientChannel> client_channel_;
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

//
// ClientChannel::ClientChannelControlHelper
//

class ClientChannel::ClientChannelControlHelper
    : public LoadBalancingPolicy::ChannelControlHelper {
 public:
  explicit ClientChannelControlHelper(
      WeakRefCountedPtr<ClientChannel> client_channel)
      : client_channel_(std::move(client_channel)) {}

  ~ClientChannelControlHelper() override {
    client_channel_.reset(DEBUG_LOCATION, "ClientChannelControlHelper");
  }

  RefCountedPtr<SubchannelInterface> CreateSubchannel(
      const grpc_resolved_address& address, const ChannelArgs& per_address_args,
      const ChannelArgs& args) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    // If shutting down, do nothing.
    if (client_channel_->resolver_ == nullptr) return nullptr;
    ChannelArgs subchannel_args = Subchannel::MakeSubchannelArgs(
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
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          *client_channel_->work_serializer_) {
    if (client_channel_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      const char* extra = client_channel_->disconnect_error_.ok()
                              ? ""
                              : " (ignoring -- channel shutting down)";
      gpr_log(GPR_INFO,
              "client_channel=%p: update: state=%s status=(%s) picker=%p%s",
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
    return client_channel_->channel_args_.GetObject<grpc_channel_credentials>()
        ->Ref();
  }

  EventEngine* GetEventEngine() override {
    return client_channel_->event_engine();
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

  WeakRefCountedPtr<ClientChannel> client_channel_;
};

//
// ClientChannel::ClientChannelCallFactory
//

class ClientChannel::ClientChannelCallFactory : public CallFactory {
 public:
  explicit ClientChannelCallFactory(RefCountedPtr<ClientChannel> client_channel)
      : CallFactory(client_channel->channel_args_),
        client_channel_(std::move(client_channel)) {}

  CallInitiator CreateCall(ClientMetadataHandle client_initial_metadata,
                           Arena* arena) override {
    // Increment call count.
    if (client_channel_->idle_timeout_ != Duration::Zero()) {
      client_channel_->idle_state_.IncreaseCallCount();
    }
    // Exit IDLE if needed.
    client_channel_->CheckConnectivityState(/*try_to_connect=*/true);
    // Create an initiator/handler pair.
    auto call = MakeCall(GetContext<EventEngine>(), arena);
    // Spawn a promise to wait for the resolver result.
    // This will eventually start using the handler, which will allow the
    // initiator to make progress.
    call.initiator.SpawnGuarded(
        "wait-for-name-resolution",
        [self = RefAsSubclass<ClientChannelCallFactory>(),
         client_initial_metadata = std::move(client_initial_metadata),
         initiator = call.initiator,
         handler = std::move(call.handler), was_queued = false]() mutable {
          const bool wait_for_ready =
              client_initial_metadata->GetOrCreatePointer(WaitForReady())
              ->value;
          return Map(
              // Wait for the resolver result.
              self->client_channel_->resolver_data_for_calls_.NextWhen(
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
                      got_result = result->config_selector != nullptr;
                    }
                    if (!got_result) was_queued = true;
                    return got_result;
                  }),
                  // Handle resolver result.
                  [self, &was_queued, &initiator, &handler,
                   client_initial_metadata =
                       std::move(client_initial_metadata)](
                      absl::StatusOr<ResolverDataForCalls> resolver_data)
                      mutable {
                    if (!resolver_data.ok()) return resolver_data.status();
                    // Apply service config to call.
                    absl::Status status =
                        self->client_channel_->ApplyServiceConfigToCall(
                            *resolver_data->config_selector,
                            client_initial_metadata);
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
                    handler.SetStack(std::move(resolver_data->filter_stack));
                    self->client_channel_->call_destination_->StartCall(
                        std::move(handler));
                    return absl::OkStatus();
                  });
        });
    // Return the initiator.
    return call.initiator;
  }

 private:
  RefCountedPtr<ClientChannel> client_channel_;
};

//
// NoRetryCallDestination
//

namespace {

ClientChannelServiceConfigCallData* GetServiceConfigCallDataFromContext() {
  auto* legacy_context = GetContext<grpc_call_context_element>();
  return static_cast<ClientChannelServiceConfigCallData*>(
      legacy_context[GRPC_CONTEXT_SERVICE_CONFIG_CALL_DATA].value);
}

// A call destination that does not support retries.
class NoRetryCallDestination : public CallDestination {
 public:
  explicit NoRetryCallDestination(
      RefCountedPtr<ClientChannel> client_channel)
      : client_channel_(std::move(client_channel)) {}

  void StartCall(CallHandler call_handler) override {
    call_handler.SpawnGuarded(
        "drain_send_initial_metadata",
        [client_channel = client_channel_,
         call_handler = std::move(call_handler)]() mutable {
          // Wait to get client initial metadata from the call handler.
          return Map(
              call_handler.PullClientInitialMetadata(),
              [client_channel = std::move(client_channel), call_handler](
                  ValueOrFailure<ClientMetadataHandle> client_initial_metadata)
                  mutable {
                if (!client_initial_metadata.ok()) return;
                // Create the LoadBalancedCall.
                CallInitiator call_initiator =
                    client_channel->CreateLoadBalancedCall(
                        std::move(*client_initial_metadata),
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

//
// ClientChannel implementation
//

namespace {

RefCountedPtr<SubchannelPoolInterface> GetSubchannelPool(
    const ChannelArgs& args) {
  if (args.GetBool(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL).value_or(false)) {
    return MakeRefCounted<LocalSubchannelPool>();
  }
  return GlobalSubchannelPool::instance();
}

}  // namespace

absl::StatusOr<OrphanablePtr<Channel>> ClientChannel::Create(
    std::string target, ChannelArgs channel_args,
    grpc_channel_stack_type channel_stack_type,
    grpc_compression_options compression_options) {
  GPR_ASSERT(channel_stack_type == GRPC_CLIENT_CHANNEL);
  // Get URI to resolve, using proxy mapper if needed.
  if (target.empty()) {
    return absl::InternalError("target URI is empty in client channel");
  }
  std::string uri_to_resolve = CoreConfiguration::Get()
                                   .proxy_mapper_registry()
                                   .MapName(target, &channel_args)
                                   .value_or(target);
  // Make sure the URI to resolve is valid, so that we know that
  // resolver creation will succeed later.
  if (!CoreConfiguration::Get().resolver_registry().IsValidTarget(
          uri_to_resolve)) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid target URI: ", uri_to_resolve));
  }
  // Get default service config.  If none is specified via the client API,
  // we use an empty config.
  absl::optional<absl::string_view> service_config_json =
      channel_args.GetString(GRPC_ARG_SERVICE_CONFIG);
  if (!service_config_json.has_value()) service_config_json = "{}";
  auto default_service_config =
      ServiceConfigImpl::Create(channel_args, *service_config_json);
  if (!default_service_config.ok()) return default_service_config.status();
  // Strip out service config channel arg, so that it doesn't affect
  // subchannel uniqueness when the args flow down to that layer.
  channel_args = channel_args.Remove(GRPC_ARG_SERVICE_CONFIG);
  // Check client channel factory.
  auto* client_channel_factory =
      channel_args.GetObject<ClientChannelFactory>();
  if (client_channel_factory == nullptr) {
    return absl::InternalError(
        "Missing client channel factory in args for client channel");
  }
  // Success.  Construct channel.
  return MakeOrphanable<ClientChannel>(
      std::move(target), std::move(channel_args),
      compression_options, std::move(uri_to_resolve),
      std::move(*default_service_config), client_channel_factory);
}

ClientChannel::ClientChannel(
    std::string target, ChannelArgs channel_args,
    grpc_compression_options compression_options,
    std::string uri_to_resolve,
    RefCountedPtr<ServiceConfig> default_service_config,
    ClientChannelFactory* client_channel_factory)
    : Channel(std::move(target), channel_args, compression_options),
      channel_args_(std::move(channel_args)),
      uri_to_resolve_(std::move(uri_to_resolve)),
      service_config_parser_index_(
          internal::ClientChannelServiceConfigParser::ParserIndex()),
      default_service_config_(std::move(default_service_config)),
      client_channel_factory_(client_channel_factory),
      channelz_node_(channel_args_.GetObject<channelz::ChannelNode>()),
      lb_call_size_estimator_(1024),
      lb_call_allocator_(channel_args_.GetObject<ResourceQuota>()
                             ->memory_quota()
                             ->CreateMemoryOwner()),
      idle_timeout_(GetClientIdleTimeout(channel_args_)),
      resolver_data_for_calls_(ResolverDataForCalls{}),
      picker_(nullptr),
      work_serializer_(
          std::make_shared<WorkSerializer>(GetContext<EventEngine>())),
      state_tracker_("client_channel", GRPC_CHANNEL_IDLE),
      subchannel_pool_(GetSubchannelPool(channel_args_)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: creating client_channel", this);
  }
  // Create call factory.
  call_factory_ =
      MakeRefCounted<ClientChannelCallFactory>(RefAsSubclass<ClientChannel>());
  // Create call destination.
  const bool enable_retries =
      !channel_args_.WantMinimalStack() &&
      channel_args_.GetBool(GRPC_ARG_ENABLE_RETRIES).value_or(true);
  if (enable_retries) {
    Crash("call v3 stack does not yet support retries");
  } else {
    call_destination_ = MakeOrphanable<NoRetryCallDestination>();
  }
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
            target);
  } else {
    default_authority_ = std::move(*default_authority);
  }
}

ClientChannel::~ClientChannel() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: destroying", this);
  }
}

void ClientChannel::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: shutting down", this);
  }
  DestroyResolverAndLbPolicyLocked();
  // IncreaseCallCount() introduces a phony call and prevents the idle
  // timer from being reset by other threads.
  idle_state_.IncreaseCallCount();
  idle_activity_.Reset();
}

grpc_connectivity_state ClientChannel::CheckConnectivityState(
    bool try_to_connect) {
  // state_tracker_ is guarded by work_serializer_, which we're not
  // holding here.  But the one method of state_tracker_ that *is*
  // thread-safe to call without external synchronization is the state()
  // method, so we can disable thread-safety analysis for this one read.
  grpc_connectivity_state state =
      ABSL_TS_UNCHECKED_READ(state_tracker_).state();
  if (state == GRPC_CHANNEL_IDLE && try_to_connect) {
    work_serializer_->Run(
        [self = WeakRefAsSubclass<ClientChannel>()]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(*work_serializer_) {
          self->TryToConnectLocked();
        },
        DEBUG_LOCATION);
  }
  return state;
}

void ClientChannel::AddConnectivityWatcher(
    grpc_connectivity_state initial_state,
    OrphanablePtr<AsyncConnectivityStateWatcherInterface> watcher) {
  work_serializer_->Run(
      [self = WeakRefAsSubclass<ClientChannel>(), initial_state,
       watcher = std::move(watcher)]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(*work_serializer_) {
        self->state_tracker_.AddWatcher(initial_state, std::move(watcher));
      },
      DEBUG_LOCATION);
}

void ClientChannel::RemoveConnectivityWatcher(
    AsyncConnectivityStateWatcherInterface* watcher) {
  work_serializer_->Run(
      [self = WeakRefAsSubclass<ClientChannel>(), watcher]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(*work_serializer_) {
        self->state_tracker_.RemoveWatcher(watcher);
      },
      DEBUG_LOCATION);
}

void ClientChannel::GetInfo(const grpc_channel_info* info) {
  MutexLock lock(&info_mu_);
  if (info->lb_policy_name != nullptr) {
    *info->lb_policy_name = gpr_strdup(info_lb_policy_name_.c_str());
  }
  if (info->service_config_json != nullptr) {
    *info->service_config_json = gpr_strdup(info_service_config_json_.c_str());
  }
}

void ClientChannel::ResetConnectionBackoff() {
  if (lb_policy_ != nullptr) lb_policy_->ResetBackoffLocked();
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

// A class to handle CQ completion for a ping.
class PingRequest {
 public:
  PingRequest(grpc_completion_queue* cq, void* tag) : cq_(cq), tag_(tag) {
    grpc_cq_begin_op(cq, tag);
  }

  // Triggers CQ completion and eventually deletes the PingRequest object.
  void Complete(grpc_error_handle error) {
    grpc_cq_end_op(cq_, tag_, error, Destroy, this, &completion_storage_);
  }

 private:
  static void Destroy(void* arg, grpc_cq_completion* /*storage*/) {
    delete static_cast<PingRequest*>(arg);
  }

  grpc_completion_queue* cq_;
  void* tag_;
  grpc_cq_completion completion_storage_;
};

}  // namespace

void ClientChannel::Ping(grpc_completion_queue* cq, void* tag) {
  auto* request = new PingRequest(cq, tag);
  // Get picker.
  auto picker = NowOrNever(picker_.NextWhen(
      [](const RefCountedPtr<LoadBalancingPolicy::SubchannelPicker>& picker) {
        return true;
      }));
  if (picker == nullptr) {
    request->Complete(absl::UnavailableError("channel not connected"));
    return;
  }
  // Do pick.
  LoadBalancingPolicy::PickResult result =
      picker->Pick(LoadBalancingPolicy::PickArgs());
  HandlePickResult<grpc_error_handle>(
      &result,
      // Complete pick.
      [&](LoadBalancingPolicy::PickResult::Complete* complete_pick) {
        SubchannelWrapper* subchannel = static_cast<SubchannelWrapper*>(
            complete_pick->subchannel.get());
        RefCountedPtr<ConnectedSubchannel> connected_subchannel =
            subchannel->connected_subchannel();
        if (connected_subchannel == nullptr) {
          request->Complete(
              absl::UnavailableError("LB pick for ping not connected"));
          return;
        }
        connected_subchannel->Ping([request](absl::Status status) {
          request->Complete(std::move(status));
        });
      },
      // Queue pick.
      [](LoadBalancingPolicy::PickResult::Queue* /*queue_pick*/) {
        request->Complete(absl::UnavailableError("LB picker queued call"));
      },
      // Fail pick.
      [](LoadBalancingPolicy::PickResult::Fail* fail_pick) {
        request->Complete(fail_pick->status);
      },
      // Drop pick.
      [](LoadBalancingPolicy::PickResult::Drop* drop_pick) {
        request->Complete(drop_pick->status);
      });
}

grpc_call* ClientChannel::CreateCall(
    grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, grpc_pollset_set* /*pollset_set_alternative*/,
    Slice path, absl::optional<Slice> authority, Timestamp deadline,
    bool registered_method) {
// FIXME: code to convert from C-core batch API to v3 call, then invoke
// call_factory_->CreateCall()
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

void ClientChannel::CreateResolverLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: starting name resolution for %s",
            this, uri_to_resolve_.c_str());
  }
  resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      uri_to_resolve_, channel_args_,
      interested_parties_, // FIXME: remove somehow
      work_serializer_,
      std::make_unique<ResolverResultHandler>(
          WeakRefAsSubclass<ClientChannel>()));
  // Since the validity of the args was checked when the channel was created,
  // CreateResolver() must return a non-null result.
  GPR_ASSERT(resolver_ != nullptr);
  UpdateStateLocked(GRPC_CHANNEL_CONNECTING, absl::Status(),
                    "started resolving");
  resolver_->StartLocked();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: created resolver=%p", this,
            resolver_.get());
  }
}

void ClientChannel::DestroyResolverAndLbPolicyLocked() {
  if (resolver_ != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: shutting down resolver=%p", this,
              resolver_.get());
    }
    resolver_.reset();
    saved_service_config_.reset();
    saved_config_selector_.reset();
    resolver_data_for_calls_.Set(ResolverDataForCalls{nullptr, nullptr});
    // Clear LB policy if set.
    if (lb_policy_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO, "client_channel=%p: shutting down lb_policy=%p",
                this, lb_policy_.get());
      }
      lb_policy_.reset();
      picker_.Set(nullptr);
    }
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
      gpr_log(GPR_INFO,
              "client_channel=%p: resolver returned service config error: %s",
              this, result.service_config.status().ToString().c_str());
    }
    // If the service config was invalid, then fallback to the
    // previously returned service config, if any.
    if (saved_service_config_ != nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
        gpr_log(GPR_INFO,
                "client_channel=%p: resolver returned invalid service config; "
                "continuing to use previous service config",
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
              "client_channel=%p: resolver returned no service config; "
              "using default service config for channel",
              this);
    }
    service_config = default_service_config_;
  } else {
    // Use ServiceConfig and ConfigSelector returned by resolver.
    service_config = std::move(*result.service_config);
    config_selector = result.args.GetObjectRef<ConfigSelector>();
  }
  // Note: The only case in which service_config is null here is if the
  // resolver returned a service config error and we don't have a previous
  // service config to fall back to.
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
      // TODO(ncteisen): might be worth somehow including a snippet of the
      // config in the trace, at the risk of bloating the trace logs.
      trace_strings.push_back("Service config changed");
    } else if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
      gpr_log(GPR_INFO, "client_channel=%p: service config not changed", this);
    }
    // Create or update LB policy, as needed.
    resolver_result_status = CreateOrUpdateLbPolicyLocked(
        std::move(lb_policy_config),
        parsed_service_config->health_check_service_name(), std::move(result));
    // Start using new service config for calls.
    // This needs to happen after the LB policy has been updated, since
    // the ConfigSelector may need the LB policy to know about new
    // destinations before it can send RPCs to those destinations.
    if (service_config_changed || config_selector_changed) {
      UpdateServiceConfigInDataPlaneLocked();
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
    // Send updated resolver result.
    resolver_data_for_calls_.Set(
          MaybeRewriteIllegalStatusCode(status, "resolver"));
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
      std::make_unique<ClientChannelControlHelper>(
          WeakRefAsSubclass<ClientChannel>());
  lb_policy_args.args = args;
  OrphanablePtr<LoadBalancingPolicy> lb_policy =
      MakeOrphanable<ChildPolicyHandler>(std::move(lb_policy_args),
                                         &grpc_client_channel_trace);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: created new LB policy %p", this,
            lb_policy.get());
  }
  return lb_policy;
}

void ClientChannel::UpdateServiceConfigInControlPlaneLocked(
    RefCountedPtr<ServiceConfig> service_config,
    RefCountedPtr<ConfigSelector> config_selector, std::string lb_policy_name) {
  std::string service_config_json(service_config->json_string());
  // Update service config.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: using service config: \"%s\"", this,
            service_config_json.c_str());
  }
  saved_service_config_ = std::move(service_config);
  // Update config selector.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: using ConfigSelector %p", this,
            config_selector.get());
  }
  saved_config_selector_ = std::move(config_selector);
  // Update the data used by GetChannelInfo().
  {
    MutexLock lock(&info_mu_);
    info_lb_policy_name_ = std::move(lb_policy_name);
    info_service_config_json_ = std::move(service_config_json);
  }
}

void ClientChannel::UpdateServiceConfigInDataPlaneLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: switching to ConfigSelector %p", this,
            saved_config_selector_.get());
  }
  // Use default config selector if resolver didn't supply one.
  RefCountedPtr<ConfigSelector> config_selector = saved_config_selector_;
  if (config_selector == nullptr) {
    config_selector =
        MakeRefCounted<DefaultConfigSelector>(saved_service_config_);
  }
  // Construct filter stack.
  CallFilters::StackBuilder builder;
  if (idle_timeout_ != Duration::Zero()) {
    builder.AddOnServerTrailingMetadata(
        [this](ServerMetadataHandle&) {
          if (idle_state_.DecreaseCallCount()) StartIdleTimer();
        });
  }
// FIXME: add filters registered for CLIENT_CHANNEL plus filters returned
// by config selector
#if 0
  std::vector<const grpc_channel_filter*> filters =
      config_selector->GetFilters();
  ChannelArgs new_args =
      channel_args_.SetObject(this).SetObject(service_config);
  RefCountedPtr<DynamicFilters> dynamic_filters =
      DynamicFilters::Create(new_args, std::move(filters));
  GPR_ASSERT(dynamic_filters != nullptr);
#endif
  auto filter_stack = builder.Build();
  // Send result to data plane.
  resolver_data_for_calls_.Set(
      ResolverDataForCalls{std::move(config_selector),
                           std::move(filter_stack)});
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
  picker_.Set(std::move(picker));
}

void ClientChannel::StartIdleTimer() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: idle timer started", this);
  }
  auto self = WeakRefAsSubclass<ClientChannel>();
  auto promise = Loop([self]() {
    return TrySeq(Sleep(Timestamp::Now() + self->idle_timeout_),
                  [&self]() -> Poll<LoopCtl<absl::Status>> {
                    if (self->idle_state_.CheckTimer()) {
                      return Continue{};
                    } else {
                      return absl::OkStatus();
                    }
                  });
  });
  idle_activity_.Set(MakeActivity(
      std::move(promise), ExecCtxWakeupScheduler{},
      [self = std::move(self)](absl::Status status) mutable {
        if (status.ok()) {
          auto* self_ptr = self.get();
          self_ptr->work_serializer_->Run(
              [self = std::move(self)]() {
                self->DestroyResolverAndLbPolicyLocked();
                self->UpdateStateAndPickerLocked(
                    GRPC_CHANNEL_IDLE, absl::OkStatus(),
                    "channel entering IDLE", nullptr);
                // TODO(roth): In case there's a race condition, we
                // might need to check for any calls that are queued
                // waiting for a resolver result or an LB pick.
              },
              DEBUG_LOCATION);
        }
      },
      GetContext<EventEngine>()));
}

absl::Status ClientChannel::ApplyServiceConfigToCall(
    ConfigSelector& config_selector,
    ClientMetadataHandle& client_initial_metadata) const {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_call_trace)) {
    gpr_log(GPR_INFO, "client_channel=%p: %sapplying service config to call",
            this, GetContext<Activity>()->DebugTag().c_str());
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
                  "client_channel=%p: %sLB pick succeeded: subchannel=%p",
                  this, GetContext<Activity>()->DebugTag().c_str(),
                  complete_pick->subchannel.get());
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
                    "client_channel=%p: %ssubchannel returned by LB picker "
                    "has no connected subchannel; queueing pick",
                    this, GetContext<Activity>()->DebugTag().c_str());
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
          gpr_log(GPR_INFO, "client_channel=%p: %sLB pick queued",
                  this, GetContext<Activity>()->DebugTag().c_str());
        }
        return Continue{};
      },
      // FailPick
      [this, &error](LoadBalancingPolicy::PickResult::Fail* fail_pick) {
        if (GRPC_TRACE_FLAG_ENABLED(grpc_client_channel_lb_call_trace)) {
          gpr_log(GPR_INFO, "client_channel=%p: %sLB pick failed: %s",
                  this, GetContext<Activity>()->DebugTag().c_str(),
                  fail_pick->status.ToString().c_str());
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
          gpr_log(GPR_INFO, "client_channel=%p: %sLB pick dropped: %s",
                  this, GetContext<Activity>()->DebugTag().c_str(),
                  drop_pick->status.ToString().c_str());
        }
        return grpc_error_set_int(
            MaybeRewriteIllegalStatusCode(std::move(drop_pick->status),
                                          "LB drop"),
            StatusIntProperty::kLbPolicyDrop, 1);
      });
}

}  // namespace grpc_core
