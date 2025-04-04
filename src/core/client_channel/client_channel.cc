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

#include "src/core/client_channel/client_channel.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/json.h>
#include <grpc/support/metrics.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <functional>
#include <new>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/client_call.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/call/status_util.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/client_channel_service_config.h"
#include "src/core/client_channel/config_selector.h"
#include "src/core/client_channel/dynamic_filters.h"
#include "src/core/client_channel/global_subchannel_pool.h"
#include "src/core/client_channel/local_subchannel_pool.h"
#include "src/core/client_channel/retry_interceptor.h"
#include "src/core/client_channel/subchannel.h"
#include "src/core/client_channel/subchannel_interface_internal.h"
#include "src/core/config/core_configuration.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/ext/filters/channel_idle/legacy_channel_idle_filter.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/load_balancing/child_policy_handler.h"
#include "src/core/load_balancing/lb_policy.h"
#include "src/core/load_balancing/lb_policy_registry.h"
#include "src/core/load_balancing/subchannel_interface.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver_registry.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/crash.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/json/json.h"
#include "src/core/util/sync.h"
#include "src/core/util/useful.h"
#include "src/core/util/work_serializer.h"

namespace grpc_core {

using grpc_event_engine::experimental::EventEngine;

using internal::ClientChannelMethodParsedConfig;

//
// ClientChannel::ResolverResultHandler
//

class ClientChannel::ResolverResultHandler : public Resolver::ResultHandler {
 public:
  explicit ResolverResultHandler(
      WeakRefCountedPtr<ClientChannel> client_channel)
      : client_channel_(std::move(client_channel)) {}

  ~ResolverResultHandler() override {
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << client_channel_.get()
        << ": resolver shutdown complete";
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
class ClientChannel::SubchannelWrapper
    : public SubchannelInterfaceWithCallDestination {
 public:
  SubchannelWrapper(WeakRefCountedPtr<ClientChannel> client_channel,
                    RefCountedPtr<Subchannel> subchannel);
  ~SubchannelWrapper() override;

  void Orphaned() override;
  void WatchConnectivityState(
      std::unique_ptr<ConnectivityStateWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_);
  void CancelConnectivityStateWatch(
      ConnectivityStateWatcherInterface* watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_);

  RefCountedPtr<UnstartedCallDestination> call_destination() override {
    return subchannel_->call_destination();
  }

  void RequestConnection() override { subchannel_->RequestConnection(); }

  void ResetBackoff() override { subchannel_->ResetBackoff(); }

  void AddDataWatcher(std::unique_ptr<DataWatcherInterface> watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_);
  void CancelDataWatcher(DataWatcherInterface* watcher) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_);
  void ThrottleKeepaliveTime(int new_keepalive_time);
  std::string address() const override { return subchannel_->address(); }

 private:
  class WatcherWrapper;

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
// keepalive information between subchannels.
class ClientChannel::SubchannelWrapper::WatcherWrapper
    : public Subchannel::ConnectivityStateWatcherInterface {
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
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << subchannel_wrapper_->client_channel_.get()
        << ": connectivity change for subchannel wrapper "
        << subchannel_wrapper_.get() << " subchannel "
        << subchannel_wrapper_->subchannel_.get()
        << "; hopping into work_serializer";
    self.release();  // Held by callback.
    subchannel_wrapper_->client_channel_->work_serializer_->Run(
        [this, state, status]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
            *subchannel_wrapper_->client_channel_->work_serializer_) {
          ApplyUpdateInControlPlaneWorkSerializer(state, status);
          Unref();
        });
  }

  grpc_pollset_set* interested_parties() override { return nullptr; }

 private:
  void ApplyUpdateInControlPlaneWorkSerializer(grpc_connectivity_state state,
                                               const absl::Status& status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          *subchannel_wrapper_->client_channel_->work_serializer_) {
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << subchannel_wrapper_->client_channel_.get()
        << ": processing connectivity change in work serializer for subchannel "
           "wrapper "
        << subchannel_wrapper_.get() << " subchannel "
        << subchannel_wrapper_->subchannel_.get()
        << " watcher=" << watcher_.get()
        << " state=" << ConnectivityStateName(state) << " status=" << status;
    auto keepalive_throttling = status.GetPayload(kKeepaliveThrottlingKey);
    if (keepalive_throttling.has_value()) {
      int new_keepalive_time = -1;
      if (absl::SimpleAtoi(std::string(keepalive_throttling.value()),
                           &new_keepalive_time)) {
        if (new_keepalive_time >
            subchannel_wrapper_->client_channel_->keepalive_time_) {
          subchannel_wrapper_->client_channel_->keepalive_time_ =
              new_keepalive_time;
          GRPC_TRACE_LOG(client_channel, INFO)
              << "client_channel=" << subchannel_wrapper_->client_channel_.get()
              << ": throttling keepalive time to "
              << subchannel_wrapper_->client_channel_->keepalive_time_;
          // Propagate the new keepalive time to all subchannels. This is so
          // that new transports created by any subchannel (and not just the
          // subchannel that received the GOAWAY), use the new keepalive time.
          for (auto* subchannel_wrapper :
               subchannel_wrapper_->client_channel_->subchannel_wrappers_) {
            subchannel_wrapper->ThrottleKeepaliveTime(new_keepalive_time);
          }
        }
      } else {
        LOG(ERROR) << "client_channel="
                   << subchannel_wrapper_->client_channel_.get()
                   << ": Illegal keepalive throttling value "
                   << std::string(keepalive_throttling.value());
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

ClientChannel::SubchannelWrapper::SubchannelWrapper(
    WeakRefCountedPtr<ClientChannel> client_channel,
    RefCountedPtr<Subchannel> subchannel)
    : SubchannelInterfaceWithCallDestination(
          GRPC_TRACE_FLAG_ENABLED(client_channel) ? "SubchannelWrapper"
                                                  : nullptr),
      client_channel_(std::move(client_channel)),
      subchannel_(std::move(subchannel)) {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << client_channel_.get()
      << ": creating subchannel wrapper " << this << " for subchannel "
      << subchannel_.get();
#ifndef NDEBUG
  DCHECK(client_channel_->work_serializer_->RunningInWorkSerializer());
#endif
  if (client_channel_->channelz_node_ != nullptr) {
    auto* subchannel_node = subchannel_->channelz_node();
    if (subchannel_node != nullptr) {
      auto it =
          client_channel_->subchannel_refcount_map_.find(subchannel_.get());
      if (it == client_channel_->subchannel_refcount_map_.end()) {
        client_channel_->channelz_node_->AddChildSubchannel(
            subchannel_node->uuid());
        it = client_channel_->subchannel_refcount_map_
                 .emplace(subchannel_.get(), 0)
                 .first;
      }
      ++it->second;
    }
  }
  client_channel_->subchannel_wrappers_.insert(this);
}

ClientChannel::SubchannelWrapper::~SubchannelWrapper() {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << client_channel_.get()
      << ": destroying subchannel wrapper " << this << " for subchannel "
      << subchannel_.get();
}

void ClientChannel::SubchannelWrapper::Orphaned() {
  // Make sure we clean up the channel's subchannel maps inside the
  // WorkSerializer.
  auto self = WeakRefAsSubclass<SubchannelWrapper>(DEBUG_LOCATION,
                                                   "subchannel map cleanup");
  client_channel_->work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
          *self->client_channel_->work_serializer_) {
        self->client_channel_->subchannel_wrappers_.erase(self.get());
        if (self->client_channel_->channelz_node_ != nullptr) {
          auto* subchannel_node = self->subchannel_->channelz_node();
          if (subchannel_node != nullptr) {
            auto it = self->client_channel_->subchannel_refcount_map_.find(
                self->subchannel_.get());
            CHECK(it != self->client_channel_->subchannel_refcount_map_.end());
            --it->second;
            if (it->second == 0) {
              self->client_channel_->channelz_node_->RemoveChildSubchannel(
                  subchannel_node->uuid());
              self->client_channel_->subchannel_refcount_map_.erase(it);
            }
          }
        }
      });
}

void ClientChannel::SubchannelWrapper::WatchConnectivityState(
    std::unique_ptr<ConnectivityStateWatcherInterface> watcher) {
  auto& watcher_wrapper = watcher_map_[watcher.get()];
  CHECK(watcher_wrapper == nullptr);
  watcher_wrapper = new WatcherWrapper(
      std::move(watcher),
      RefAsSubclass<SubchannelWrapper>(DEBUG_LOCATION, "WatcherWrapper"));
  subchannel_->WatchConnectivityState(
      RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface>(
          watcher_wrapper));
}

void ClientChannel::SubchannelWrapper::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  auto it = watcher_map_.find(watcher);
  CHECK(it != watcher_map_.end());
  subchannel_->CancelConnectivityStateWatch(it->second);
  watcher_map_.erase(it);
}

void ClientChannel::SubchannelWrapper::AddDataWatcher(
    std::unique_ptr<DataWatcherInterface> watcher) {
  static_cast<InternalSubchannelDataWatcherInterface*>(watcher.get())
      ->SetSubchannel(subchannel_.get());
  CHECK(data_watchers_.insert(std::move(watcher)).second);
}

void ClientChannel::SubchannelWrapper::CancelDataWatcher(
    DataWatcherInterface* watcher) {
  auto it = data_watchers_.find(watcher);
  if (it != data_watchers_.end()) data_watchers_.erase(it);
}

void ClientChannel::SubchannelWrapper::ThrottleKeepaliveTime(
    int new_keepalive_time) {
  subchannel_->ThrottleKeepaliveTime(new_keepalive_time);
}

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

  void UpdateState(
      grpc_connectivity_state state, const absl::Status& status,
      RefCountedPtr<LoadBalancingPolicy::SubchannelPicker> picker) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*client_channel_->work_serializer_) {
    if (client_channel_->resolver_ == nullptr) return;  // Shutting down.
    if (GRPC_TRACE_FLAG_ENABLED(client_channel)) {
      const char* extra = client_channel_->disconnect_error_.ok()
                              ? ""
                              : " (ignoring -- channel shutting down)";
      LOG(INFO) << "client_channel=" << client_channel_.get()
                << ": update: state=" << ConnectivityStateName(state)
                << " status=(" << status << ") picker=" << picker.get()
                << extra;
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
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << client_channel_.get()
        << ": started name re-resolving";
    client_channel_->resolver_->RequestReresolutionLocked();
  }

  absl::string_view GetTarget() override { return client_channel_->target(); }

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

  GlobalStatsPluginRegistry::StatsPluginGroup& GetStatsPluginGroup() override {
    return *client_channel_->stats_plugin_group_;
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

absl::StatusOr<RefCountedPtr<Channel>> ClientChannel::Create(
    std::string target, ChannelArgs channel_args) {
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
  std::optional<absl::string_view> service_config_json =
      channel_args.GetString(GRPC_ARG_SERVICE_CONFIG);
  if (!service_config_json.has_value()) service_config_json = "{}";
  auto default_service_config =
      ServiceConfigImpl::Create(channel_args, *service_config_json);
  if (!default_service_config.ok()) return default_service_config.status();
  // Strip out service config channel arg, so that it doesn't affect
  // subchannel uniqueness when the args flow down to that layer.
  channel_args = channel_args.Remove(GRPC_ARG_SERVICE_CONFIG);
  // Check client channel factory.
  auto* client_channel_factory = channel_args.GetObject<ClientChannelFactory>();
  if (client_channel_factory == nullptr) {
    return absl::InternalError(
        "Missing client channel factory in args for client channel");
  }
  auto* call_destination_factory =
      channel_args.GetObject<CallDestinationFactory>();
  if (call_destination_factory == nullptr) {
    return absl::InternalError(
        "Missing call destination factory in args for client channel");
  }
  if (channel_args.GetObject<EventEngine>() == nullptr) {
    return absl::InternalError(
        "Missing event engine in args for client channel");
  }
  // Success.  Construct channel.
  return MakeRefCounted<ClientChannel>(
      std::move(target), std::move(channel_args), std::move(uri_to_resolve),
      std::move(*default_service_config), client_channel_factory,
      call_destination_factory);
}

namespace {

std::string GetDefaultAuthorityFromChannelArgs(const ChannelArgs& channel_args,
                                               absl::string_view target) {
  std::optional<std::string> default_authority =
      channel_args.GetOwnedString(GRPC_ARG_DEFAULT_AUTHORITY);
  if (!default_authority.has_value()) {
    return CoreConfiguration::Get().resolver_registry().GetDefaultAuthority(
        target);
  } else {
    return std::move(*default_authority);
  }
}

std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
GetStatsPluginGroupFromChannelArgs(const ChannelArgs& channel_args,
                                   absl::string_view target,
                                   absl::string_view default_authority) {
  grpc_event_engine::experimental::ChannelArgsEndpointConfig endpoint_config(
      channel_args);
  experimental::StatsPluginChannelScope scope(target, default_authority,
                                              endpoint_config);
  return GlobalStatsPluginRegistry::GetStatsPluginsForChannel(scope);
}

}  // namespace

ClientChannel::ClientChannel(
    std::string target, ChannelArgs channel_args, std::string uri_to_resolve,
    RefCountedPtr<ServiceConfig> default_service_config,
    ClientChannelFactory* client_channel_factory,
    CallDestinationFactory* call_destination_factory)
    : Channel(std::move(target), channel_args),
      default_authority_(
          GetDefaultAuthorityFromChannelArgs(channel_args, this->target())),
      stats_plugin_group_(GetStatsPluginGroupFromChannelArgs(
          channel_args, this->target(), default_authority_)),
      channel_args_(channel_args.SetObject(stats_plugin_group_)),
      event_engine_(channel_args_.GetObjectRef<EventEngine>()),
      uri_to_resolve_(std::move(uri_to_resolve)),
      service_config_parser_index_(
          internal::ClientChannelServiceConfigParser::ParserIndex()),
      default_service_config_(std::move(default_service_config)),
      client_channel_factory_(client_channel_factory),
      channelz_node_(channel_args_.GetObject<channelz::ChannelNode>()),
      idle_timeout_(GetClientIdleTimeout(channel_args_)),
      resolver_data_for_calls_(ResolverDataForCalls{}),
      picker_(nullptr),
      call_destination_(
          call_destination_factory->CreateCallDestination(picker_)),
      work_serializer_(std::make_shared<WorkSerializer>(event_engine_)),
      state_tracker_("client_channel", GRPC_CHANNEL_IDLE),
      subchannel_pool_(GetSubchannelPool(channel_args_)) {
  CHECK(event_engine_.get() != nullptr);
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": creating client_channel";
  // Set initial keepalive time.
  auto keepalive_arg = channel_args_.GetInt(GRPC_ARG_KEEPALIVE_TIME_MS);
  if (keepalive_arg.has_value()) {
    keepalive_time_ = Clamp(*keepalive_arg, 1, INT_MAX);
  } else {
    keepalive_time_ = -1;  // unset
  }
}

ClientChannel::~ClientChannel() {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": destroying";
}

void ClientChannel::Orphaned() {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": shutting down";
  // Weird capture then copy needed to satisfy thread safety analysis,
  // otherwise it seems to fail to recognize the correct lock is taken in the
  // lambda.
  auto self = WeakRefAsSubclass<ClientChannel>();
  work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*self->work_serializer_) {
        self->DestroyResolverAndLbPolicyLocked();
      });
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
    auto self = WeakRefAsSubclass<ClientChannel>();  // Held by callback.
    work_serializer_->Run(
        [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*self->work_serializer_) {
          self->TryToConnectLocked();
        });
  }
  return state;
}

namespace {

// A fire-and-forget object to handle external connectivity state watches.
class ExternalStateWatcher : public RefCounted<ExternalStateWatcher> {
 public:
  ExternalStateWatcher(WeakRefCountedPtr<ClientChannel> channel,
                       grpc_completion_queue* cq, void* tag,
                       grpc_connectivity_state last_observed_state,
                       Timestamp deadline)
      : channel_(std::move(channel)), cq_(cq), tag_(tag) {
    MutexLock lock(&mu_);
    // Start watch.  This inherits the ref from creation.
    auto watcher =
        MakeOrphanable<Watcher>(RefCountedPtr<ExternalStateWatcher>(this));
    watcher_ = watcher.get();
    channel_->AddConnectivityWatcher(last_observed_state, std::move(watcher));
    // Start timer.  This takes a second ref.
    const Duration timeout = deadline - Timestamp::Now();
    timer_handle_ =
        channel_->event_engine()->RunAfter(timeout, [self = Ref()]() mutable {
          ExecCtx exec_ctx;
          self->MaybeStartCompletion(absl::DeadlineExceededError(
              "Timed out waiting for connection state change"));
          // ExternalStateWatcher deletion might require an active ExecCtx.
          self.reset();
        });
  }

 private:
  class Watcher : public AsyncConnectivityStateWatcherInterface {
   public:
    explicit Watcher(RefCountedPtr<ExternalStateWatcher> external_state_watcher)
        : external_state_watcher_(std::move(external_state_watcher)) {}

    void OnConnectivityStateChange(grpc_connectivity_state /*new_state*/,
                                   const absl::Status& /*status*/) override {
      external_state_watcher_->MaybeStartCompletion(absl::OkStatus());
    }

   private:
    RefCountedPtr<ExternalStateWatcher> external_state_watcher_;
  };

  // This is called both when the watch reports a new connectivity state
  // and when the timer fires.  It will trigger a CQ notification only
  // on the first call.  Subsequent calls will be ignored, because
  // events can come in asynchronously.
  void MaybeStartCompletion(absl::Status status) {
    MutexLock lock(&mu_);
    if (watcher_ == nullptr) return;  // Ignore subsequent notifications.
    // Cancel watch.
    channel_->RemoveConnectivityWatcher(watcher_);
    watcher_ = nullptr;
    // Cancel timer.
    channel_->event_engine()->Cancel(timer_handle_);
    // Send CQ completion.
    Ref().release();  // Released in FinishedCompletion().
    grpc_cq_end_op(cq_, tag_, status, FinishedCompletion, this,
                   &completion_storage_);
  }

  // Called when the completion is returned to the CQ.
  static void FinishedCompletion(void* arg, grpc_cq_completion* /*ignored*/) {
    auto* self = static_cast<ExternalStateWatcher*>(arg);
    self->Unref();
  }

  WeakRefCountedPtr<ClientChannel> channel_;

  Mutex mu_;
  grpc_completion_queue* cq_ ABSL_GUARDED_BY(&mu_);
  void* tag_ ABSL_GUARDED_BY(&mu_);
  grpc_cq_completion completion_storage_ ABSL_GUARDED_BY(&mu_);
  Watcher* watcher_ ABSL_GUARDED_BY(&mu_) = nullptr;
  grpc_event_engine::experimental::EventEngine::TaskHandle timer_handle_
      ABSL_GUARDED_BY(&mu_);
};

}  // namespace

void ClientChannel::WatchConnectivityState(grpc_connectivity_state state,
                                           Timestamp deadline,
                                           grpc_completion_queue* cq,
                                           void* tag) {
  new ExternalStateWatcher(WeakRefAsSubclass<ClientChannel>(), cq, tag, state,
                           deadline);
}

void ClientChannel::AddConnectivityWatcher(
    grpc_connectivity_state,
    OrphanablePtr<AsyncConnectivityStateWatcherInterface>) {
  Crash("not implemented");
  // TODO(ctiller): to make this work, need to change WorkSerializer to use
  // absl::AnyInvocable<> instead of std::function<>
  //  work_serializer_->Run(
  //      [self = RefAsSubclass<ClientChannel>(), initial_state,
  //       watcher = std::move(watcher)]()
  //            ABSL_EXCLUSIVE_LOCKS_REQUIRED(*work_serializer_) {
  //        self->state_tracker_.AddWatcher(initial_state, std::move(watcher));
  //      });
}

void ClientChannel::RemoveConnectivityWatcher(
    AsyncConnectivityStateWatcherInterface* watcher) {
  auto self = RefAsSubclass<ClientChannel>();  // Held by callback.
  work_serializer_->Run(
      [self, watcher]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*self->work_serializer_) {
        self->state_tracker_.RemoveWatcher(watcher);
      });
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
  auto self = RefAsSubclass<ClientChannel>();
  work_serializer_->Run(
      [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*self->work_serializer_) {
        if (self->lb_policy_ != nullptr) self->lb_policy_->ResetBackoffLocked();
      });
}

namespace {

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

void ClientChannel::Ping(grpc_completion_queue*, void*) {
  // TODO(ctiller): implement
  Crash("not implemented");
}

grpc_call* ClientChannel::CreateCall(
    grpc_call* parent_call, uint32_t propagation_mask,
    grpc_completion_queue* cq, grpc_pollset_set* /*pollset_set_alternative*/,
    Slice path, std::optional<Slice> authority, Timestamp deadline, bool) {
  auto arena = call_arena_allocator()->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine());
  return MakeClientCall(parent_call, propagation_mask, cq, std::move(path),
                        std::move(authority), false, deadline,
                        compression_options(), std::move(arena), Ref());
}

void ClientChannel::StartCall(UnstartedCallHandler unstarted_handler) {
  // Increment call count.
  if (idle_timeout_ != Duration::Zero()) idle_state_.IncreaseCallCount();
  // Exit IDLE if needed.
  CheckConnectivityState(/*try_to_connect=*/true);
  // Spawn a promise to wait for the resolver result.
  // This will eventually start the call.
  unstarted_handler.SpawnGuardedUntilCallCompletes(
      "wait-for-name-resolution",
      [self = RefAsSubclass<ClientChannel>(), unstarted_handler]() mutable {
        const bool wait_for_ready =
            unstarted_handler.UnprocessedClientInitialMetadata()
                .GetOrCreatePointer(WaitForReady())
                ->value;
        return Map(
            // Wait for the resolver result.
            CheckDelayed(self->resolver_data_for_calls_.NextWhen(
                [wait_for_ready](
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
                  return got_result;
                })),
            // Handle resolver result.
            [self, unstarted_handler](
                std::tuple<absl::StatusOr<ResolverDataForCalls>, bool>
                    result_and_delayed) mutable {
              auto& resolver_data = std::get<0>(result_and_delayed);
              const bool was_queued = std::get<1>(result_and_delayed);
              if (!resolver_data.ok()) return resolver_data.status();
              // Apply service config to call.
              absl::Status status = self->ApplyServiceConfigToCall(
                  *resolver_data->config_selector,
                  unstarted_handler.UnprocessedClientInitialMetadata());
              if (!status.ok()) return status;
              // If the call was queued, add trace annotation.
              if (was_queued) {
                auto* call_tracer =
                    MaybeGetContext<CallTracerAnnotationInterface>();
                if (call_tracer != nullptr) {
                  call_tracer->RecordAnnotation(
                      "Delayed name resolution complete.");
                }
              }
              // Start the call on the destination provided by the
              // resolver.
              resolver_data->call_destination->StartCall(
                  std::move(unstarted_handler));
              return absl::OkStatus();
            });
      });
}

void ClientChannel::CreateResolverLocked() {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": starting name resolution for "
      << uri_to_resolve_;
  resolver_ = CoreConfiguration::Get().resolver_registry().CreateResolver(
      uri_to_resolve_, channel_args_, nullptr, work_serializer_,
      std::make_unique<ResolverResultHandler>(
          WeakRefAsSubclass<ClientChannel>()));
  // Since the validity of the args was checked when the channel was created,
  // CreateResolver() must return a non-null result.
  CHECK(resolver_ != nullptr);
  UpdateStateLocked(GRPC_CHANNEL_CONNECTING, absl::Status(),
                    "started resolving");
  resolver_->StartLocked();
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": created resolver=" << resolver_.get();
}

void ClientChannel::DestroyResolverAndLbPolicyLocked() {
  if (resolver_ != nullptr) {
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << this
        << ": shutting down resolver=" << resolver_.get();
    resolver_.reset();
    saved_service_config_.reset();
    saved_config_selector_.reset();
    resolver_data_for_calls_.Set(ResolverDataForCalls{nullptr, nullptr});
    // Clear LB policy if set.
    if (lb_policy_ != nullptr) {
      GRPC_TRACE_LOG(client_channel, INFO)
          << "client_channel=" << this
          << ": shutting down lb_policy=" << lb_policy_.get();
      lb_policy_.reset();
      picker_.Set(MakeRefCounted<LoadBalancingPolicy::DropPicker>(
          absl::UnavailableError("Channel shutdown")));
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
  std::optional<absl::string_view> policy_name;
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
        LOG(ERROR) << "LB policy: " << *policy_name
                   << " passed through channel_args must not "
                      "require a config. Using pick_first instead.";
      } else {
        LOG(ERROR) << "LB policy: " << *policy_name
                   << " passed through channel_args does not exist. "
                      "Using pick_first instead.";
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
  CHECK_OK(lb_policy_config);
  return std::move(*lb_policy_config);
}

}  // namespace

void ClientChannel::OnResolverResultChangedLocked(Resolver::Result result) {
  // Handle race conditions.
  if (resolver_ == nullptr) return;
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": got resolver result";
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
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << this
        << ": resolver returned service config error: "
        << result.service_config.status();
    // If the service config was invalid, then fallback to the
    // previously returned service config, if any.
    if (saved_service_config_ != nullptr) {
      GRPC_TRACE_LOG(client_channel, INFO)
          << "client_channel=" << this
          << ": resolver returned invalid service config; "
             "continuing to use previous service config";
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
    GRPC_TRACE_LOG(client_channel, INFO)
        << "client_channel=" << this
        << ": resolver returned no service config; using default service "
           "config for channel";
    service_config = default_service_config_;
  } else {
    // Use ServiceConfig and ConfigSelector returned by resolver.
    service_config = std::move(*result.service_config);
    config_selector = result.args.GetObjectRef<ConfigSelector>();
  }
  // Remove the config selector from channel args so that we're not holding
  // unnecessary refs that cause it to be destroyed somewhere other than in
  // the WorkSerializer.
  result.args = result.args.Remove(GRPC_ARG_CONFIG_SELECTOR);
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
    } else {
      GRPC_TRACE_LOG(client_channel, INFO)
          << "client_channel=" << this << ": service config not changed";
    }
    // Create or update LB policy, as needed.
    ChannelArgs new_args = result.args;
    resolver_result_status = CreateOrUpdateLbPolicyLocked(
        std::move(lb_policy_config),
        parsed_service_config->health_check_service_name(), std::move(result));
    // Start using new service config for calls.
    // This needs to happen after the LB policy has been updated, since
    // the ConfigSelector may need the LB policy to know about new
    // destinations before it can send RPCs to those destinations.
    if (service_config_changed || config_selector_changed) {
      UpdateServiceConfigInDataPlaneLocked(new_args);
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
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this
      << ": resolver transient failure: " << status;
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
    const std::optional<std::string>& health_check_service_name,
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
  update_args.args = std::move(result.args);
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
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": Updating child policy "
      << lb_policy_.get();
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
                                         &client_channel_trace);
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": created new LB policy "
      << lb_policy.get();
  return lb_policy;
}

void ClientChannel::UpdateServiceConfigInControlPlaneLocked(
    RefCountedPtr<ServiceConfig> service_config,
    RefCountedPtr<ConfigSelector> config_selector, std::string lb_policy_name) {
  std::string service_config_json(service_config->json_string());
  // Update service config.
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": using service config: \""
      << service_config_json << "\"";
  saved_service_config_ = std::move(service_config);
  // Update config selector.
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": using ConfigSelector "
      << config_selector.get();
  saved_config_selector_ = std::move(config_selector);
  // Update the data used by GetChannelInfo().
  {
    MutexLock lock(&info_mu_);
    info_lb_policy_name_ = std::move(lb_policy_name);
    info_service_config_json_ = std::move(service_config_json);
  }
}

void ClientChannel::UpdateServiceConfigInDataPlaneLocked(
    const ChannelArgs& args) {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": switching to ConfigSelector "
      << saved_config_selector_.get();
  // Use default config selector if resolver didn't supply one.
  RefCountedPtr<ConfigSelector> config_selector = saved_config_selector_;
  if (config_selector == nullptr) {
    config_selector =
        MakeRefCounted<DefaultConfigSelector>(saved_service_config_);
  }
  // Modify channel args.
  ChannelArgs new_args = args.SetObject(this).SetObject(saved_service_config_);
  // Construct filter stack.
  auto new_blackboard = MakeRefCounted<Blackboard>();
  InterceptionChainBuilder builder(new_args, blackboard_.get(),
                                   new_blackboard.get());
  if (idle_timeout_ != Duration::Zero()) {
    builder.AddOnServerTrailingMetadata([this](ServerMetadata&) {
      if (idle_state_.DecreaseCallCount()) StartIdleTimer();
    });
  }
  CoreConfiguration::Get().channel_init().AddToInterceptionChainBuilder(
      GRPC_CLIENT_CHANNEL, builder);
  // Add filters returned by the config selector (e.g., xDS HTTP filters).
  config_selector->AddFilters(builder);
  const bool enable_retries =
      !channel_args_.WantMinimalStack() &&
      channel_args_.GetBool(GRPC_ARG_ENABLE_RETRIES).value_or(true);
  if (enable_retries) {
    builder.Add<RetryInterceptor>();
  }
  // Create call destination.
  auto top_of_stack_call_destination = builder.Build(call_destination_);
  blackboard_ = std::move(new_blackboard);
  // Send result to data plane.
  if (!top_of_stack_call_destination.ok()) {
    resolver_data_for_calls_.Set(MaybeRewriteIllegalStatusCode(
        top_of_stack_call_destination.status(), "channel construction"));
  } else {
    resolver_data_for_calls_.Set(ResolverDataForCalls{
        std::move(config_selector), std::move(*top_of_stack_call_destination)});
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
  picker_.Set(std::move(picker));
}

void ClientChannel::StartIdleTimer() {
  GRPC_TRACE_LOG(client_channel, INFO)
      << "client_channel=" << this << ": idle timer started";
  auto self = WeakRefAsSubclass<ClientChannel>();
  auto promise = Loop([self]() {
    return TrySeq(Sleep(Timestamp::Now() + self->idle_timeout_),
                  [self]() -> Poll<LoopCtl<absl::Status>> {
                    if (self->idle_state_.CheckTimer()) {
                      return Continue{};
                    } else {
                      return absl::OkStatus();
                    }
                  });
  });
  auto arena = SimpleArenaAllocator(0)->MakeArena();
  arena->SetContext<grpc_event_engine::experimental::EventEngine>(
      event_engine());
  idle_activity_.Set(MakeActivity(
      std::move(promise), ExecCtxWakeupScheduler{},
      [self = std::move(self)](absl::Status status) mutable {
        if (status.ok()) {
          self->work_serializer_->Run(
              [self]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(*self->work_serializer_) {
                self->DestroyResolverAndLbPolicyLocked();
                self->UpdateStateAndPickerLocked(
                    GRPC_CHANNEL_IDLE, absl::OkStatus(),
                    "channel entering IDLE", nullptr);
                // TODO(roth): In case there's a race condition, we
                // might need to check for any calls that are
                // queued waiting for a resolver result or an LB
                // pick.
              });
        }
      },
      std::move(arena)));
}

absl::Status ClientChannel::ApplyServiceConfigToCall(
    ConfigSelector& config_selector,
    ClientMetadata& client_initial_metadata) const {
  GRPC_TRACE_LOG(client_channel_call, INFO)
      << "client_channel=" << this << ": " << GetContext<Activity>()->DebugTag()
      << " service config to call";
  // Create a ClientChannelServiceConfigCallData for the call.  This stores
  // a ref to the ServiceConfig and caches the right set of parsed configs
  // to use for the call.  The ClientChannelServiceConfigCallData will store
  // itself in the call context, so that it can be accessed by filters
  // below us in the stack, and it will be cleaned up when the call ends.
  auto* service_config_call_data =
      GetContext<Arena>()->New<ClientChannelServiceConfigCallData>(
          GetContext<Arena>());
  // Use the ConfigSelector to determine the config for the call.
  absl::Status call_config_status = config_selector.GetCallConfig(
      {&client_initial_metadata, GetContext<Arena>(),
       service_config_call_data});
  if (!call_config_status.ok()) {
    return MaybeRewriteIllegalStatusCode(call_config_status, "ConfigSelector");
  }
  // Apply our own method params to the call.
  auto* method_params = DownCast<ClientChannelMethodParsedConfig*>(
      service_config_call_data->GetMethodParsedConfig(
          service_config_parser_index_));
  if (method_params != nullptr) {
    // If the service config specifies a deadline, update the call's
    // deadline timer.
    if (method_params->timeout() != Duration::Zero()) {
      Call* call = GetContext<Call>();
      const Timestamp per_method_deadline =
          Timestamp::FromCycleCounterRoundUp(call->start_time()) +
          method_params->timeout();
      call->UpdateDeadline(per_method_deadline);
    }
    // If the service config set wait_for_ready and the application
    // did not explicitly set it, use the value from the service config.
    auto* wait_for_ready =
        client_initial_metadata.GetOrCreatePointer(WaitForReady());
    if (method_params->wait_for_ready().has_value() &&
        !wait_for_ready->explicitly_set) {
      wait_for_ready->value = method_params->wait_for_ready().value();
    }
  }
  return absl::OkStatus();
}

}  // namespace grpc_core
