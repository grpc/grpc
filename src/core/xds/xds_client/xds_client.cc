//
// Copyright 2018 gRPC authors.
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

#include "src/core/xds/xds_client/xds_client.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/discovery/v3/discovery.upb.h"
#include "envoy/service/discovery/v3/discovery.upbdefs.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "google/rpc/status.upb.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/uri.h"
#include "src/core/xds/xds_client/xds_api.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_locality.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/reflection/def.h"
#include "upb/text/encode.h"

#define GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_XDS_RECONNECT_JITTER 0.2
#define GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS 1000

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

//
// Internal class declarations
//

// An xds call wrapper that can restart a call upon failure. Holds a ref to
// the xds channel. The template parameter is the kind of wrapped xds call.
// TODO(roth): This is basically the same code as in LrsClient, and
// probably very similar to many other places in the codebase.
// Consider refactoring this into a common utility library somehow.
template <typename T>
class XdsClient::XdsChannel::RetryableCall final
    : public InternallyRefCounted<RetryableCall<T>> {
 public:
  explicit RetryableCall(WeakRefCountedPtr<XdsChannel> xds_channel);

  // Disable thread-safety analysis because this method is called via
  // OrphanablePtr<>, but there's no way to pass the lock annotation
  // through there.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

  void OnCallFinishedLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  T* call() const { return call_.get(); }
  XdsChannel* xds_channel() const { return xds_channel_.get(); }

  bool IsCurrentCallOnChannel() const;

 private:
  void StartNewCallLocked();
  void StartRetryTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  void OnRetryTimer();

  // The wrapped xds call that talks to the xds server. It's instantiated
  // every time we start a new call. It's null during call retry backoff.
  OrphanablePtr<T> call_;
  // The owning xds channel.
  WeakRefCountedPtr<XdsChannel> xds_channel_;

  // Retry state.
  BackOff backoff_;
  std::optional<EventEngine::TaskHandle> timer_handle_
      ABSL_GUARDED_BY(&XdsClient::mu_);

  bool shutting_down_ = false;
};

// Contains an ADS call to the xds server.
class XdsClient::XdsChannel::AdsCall final
    : public InternallyRefCounted<AdsCall> {
 public:
  // The ctor and dtor should not be used directly.
  explicit AdsCall(RefCountedPtr<RetryableCall<AdsCall>> retryable_call);

  // Disable thread-safety analysis because this method is called via
  // OrphanablePtr<>, but there's no way to pass the lock annotation
  // through there.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

  RetryableCall<AdsCall>* retryable_call() const {
    return retryable_call_.get();
  }
  XdsChannel* xds_channel() const { return retryable_call_->xds_channel(); }
  XdsClient* xds_client() const { return xds_channel()->xds_client(); }
  bool seen_response() const { return seen_response_; }

  void SubscribeLocked(const XdsResourceType* type, const XdsResourceName& name,
                       bool delay_send)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void UnsubscribeLocked(const XdsResourceType* type,
                         const XdsResourceName& name, bool delay_unsubscription)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  bool HasSubscribedResources() const;

 private:
  class AdsReadDelayHandle;

  class ResourceTimer final : public InternallyRefCounted<ResourceTimer> {
   public:
    ResourceTimer(const XdsResourceType* type, const XdsResourceName& name)
        : type_(type), name_(name) {}

    // Disable thread-safety analysis because this method is called via
    // OrphanablePtr<>, but there's no way to pass the lock annotation
    // through there.
    void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS {
      MaybeCancelTimer();
      Unref(DEBUG_LOCATION, "Orphan");
    }

    void MarkSubscriptionSendStarted()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      subscription_sent_ = true;
    }

    void MaybeMarkSubscriptionSendComplete(RefCountedPtr<AdsCall> ads_call)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      if (subscription_sent_) MaybeStartTimer(std::move(ads_call));
    }

    void MarkSeen() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      resource_seen_ = true;
      MaybeCancelTimer();
    }

    void MaybeCancelTimer() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      if (timer_handle_.has_value() &&
          ads_call_->xds_client()->engine()->Cancel(*timer_handle_)) {
        timer_handle_.reset();
        ads_call_.reset();
      }
    }

   private:
    void MaybeStartTimer(RefCountedPtr<AdsCall> ads_call)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      // Don't start timer if we've already either seen the resource or
      // marked it as non-existing.
      // Note: There are edge cases where we can have seen the resource
      // before we have sent the initial subscription request, such as
      // when we unsubscribe and then resubscribe to a given resource
      // and then get a response containing that resource, all while a
      // send_message op is in flight.
      if (resource_seen_) return;
      // Don't start timer if we haven't yet sent the initial subscription
      // request for the resource.
      if (!subscription_sent_) return;
      // Don't start timer if it's already running.
      if (timer_handle_.has_value()) return;
      // Check if we already have a cached version of this resource
      // (i.e., if this is the initial request for the resource after an
      // ADS stream restart).  If so, we don't start the timer, because
      // (a) we already have the resource and (b) the server may
      // optimize by not resending the resource that we already have.
      auto& authority_state =
          ads_call->xds_client()->authority_state_map_[name_.authority];
      ResourceState& state = authority_state.type_map[type_][name_.key];
      if (state.HasResource()) return;
      // Start timer.
      ads_call_ = std::move(ads_call);
      Duration timeout = ads_call_->xds_client()->request_timeout_;
      if (timeout == Duration::Zero()) {
        timeout = XdsDataErrorHandlingEnabled() &&
                          ads_call_->xds_channel()
                              ->server_.ResourceTimerIsTransientFailure()
                      ? Duration::Seconds(30)
                      : Duration::Seconds(15);
      }
      timer_handle_ = ads_call_->xds_client()->engine()->RunAfter(
          timeout, [self = Ref(DEBUG_LOCATION, "timer")]() {
            ExecCtx exec_ctx;
            self->OnTimer();
          });
    }

    void OnTimer() {
      {
        MutexLock lock(&ads_call_->xds_client()->mu_);
        timer_handle_.reset();
        auto& authority_state =
            ads_call_->xds_client()->authority_state_map_[name_.authority];
        ResourceState& state = authority_state.type_map[type_][name_.key];
        // We might have received the resource after the timer fired but before
        // the callback ran.
        if (!state.HasResource()) {
          GRPC_TRACE_LOG(xds_client, INFO)
              << "[xds_client " << ads_call_->xds_client() << "] xds server "
              << ads_call_->xds_channel()->server_uri()
              << ": timeout obtaining resource {type=" << type_->type_url()
              << " name="
              << XdsClient::ConstructFullXdsResourceName(
                     name_.authority, type_->type_url(), name_.key)
              << "} from xds server";
          resource_seen_ = true;
          if (XdsDataErrorHandlingEnabled() &&
              ads_call_->xds_channel()
                  ->server_.ResourceTimerIsTransientFailure()) {
            state.SetTimeout(
                absl::StrCat("timeout obtaining resource from xDS server ",
                             ads_call_->xds_channel()->server_uri()));
          } else {
            state.SetDoesNotExistOnTimeout();
          }
          ads_call_->xds_client()->NotifyWatchersOnResourceChanged(
              state.failed_status(), state.watchers(),
              ReadDelayHandle::NoWait());
        }
      }
      ads_call_.reset();
    }

    const XdsResourceType* type_;
    const XdsResourceName name_;

    RefCountedPtr<AdsCall> ads_call_;
    // True if we have sent the initial subscription request for this
    // resource on this ADS stream.
    bool subscription_sent_ ABSL_GUARDED_BY(&XdsClient::mu_) = false;
    // True if we have either (a) seen the resource in a response on this
    // stream or (b) declared the resource to not exist due to the timer
    // firing.
    bool resource_seen_ ABSL_GUARDED_BY(&XdsClient::mu_) = false;
    std::optional<EventEngine::TaskHandle> timer_handle_
        ABSL_GUARDED_BY(&XdsClient::mu_);
  };

  class StreamEventHandler final
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(RefCountedPtr<AdsCall> ads_call)
        : ads_call_(std::move(ads_call)) {}

    void OnRequestSent(bool ok) override { ads_call_->OnRequestSent(ok); }
    void OnRecvMessage(absl::string_view payload) override {
      ads_call_->OnRecvMessage(payload);
    }
    void OnStatusReceived(absl::Status status) override {
      ads_call_->OnStatusReceived(std::move(status));
    }

   private:
    RefCountedPtr<AdsCall> ads_call_;
  };

  struct ResourceTypeState {
    // Nonce and status for this resource type.
    std::string nonce;
    absl::Status status;

    // Subscribed resources of this type.
    std::map<std::string /*authority*/,
             std::map<XdsResourceKey, OrphanablePtr<ResourceTimer>>>
        subscribed_resources;
  };

  std::string CreateAdsRequest(absl::string_view type_url,
                               absl::string_view version,
                               absl::string_view nonce,
                               const std::vector<std::string>& resource_names,
                               absl::Status status) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  void SendMessageLocked(const XdsResourceType* type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  struct DecodeContext {
    upb::Arena arena;
    const XdsResourceType* type;
    std::string type_url;
    std::string version;
    std::string nonce;
    std::vector<std::string> errors;
    std::map<std::string /*authority*/, std::set<XdsResourceKey>>
        resources_seen;
    uint64_t num_valid_resources = 0;
    uint64_t num_invalid_resources = 0;
    Timestamp update_time = Timestamp::Now();
    RefCountedPtr<ReadDelayHandle> read_delay_handle;
  };
  void ParseResource(size_t idx, absl::string_view type_url,
                     absl::string_view resource_name,
                     absl::string_view serialized_resource,
                     DecodeContext* context)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void HandleServerReportedResourceError(size_t idx,
                                         absl::string_view resource_name,
                                         absl::Status status,
                                         DecodeContext* context)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  absl::Status DecodeAdsResponse(absl::string_view encoded_response,
                                 DecodeContext* context)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  void OnRequestSent(bool ok);
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  bool IsCurrentCallOnChannel() const;

  // Constructs a list of resource names of a given type for an ADS
  // request.  Also starts the timer for each resource if needed.
  std::vector<std::string> ResourceNamesForRequest(const XdsResourceType* type)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<AdsCall>> retryable_call_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall>
      streaming_call_;

  bool sent_initial_message_ = false;
  bool seen_response_ = false;

  const XdsResourceType* send_message_pending_
      ABSL_GUARDED_BY(&XdsClient::mu_) = nullptr;

  // Resource types for which requests need to be sent.
  std::set<const XdsResourceType*> buffered_requests_;

  // State for each resource type.
  std::map<const XdsResourceType*, ResourceTypeState> state_map_;
};

//
// XdsClient::XdsChannel::ConnectivityFailureWatcher
//

class XdsClient::XdsChannel::ConnectivityFailureWatcher
    : public XdsTransportFactory::XdsTransport::ConnectivityFailureWatcher {
 public:
  explicit ConnectivityFailureWatcher(WeakRefCountedPtr<XdsChannel> xds_channel)
      : xds_channel_(std::move(xds_channel)) {}

  void OnConnectivityFailure(absl::Status status) override {
    xds_channel_->OnConnectivityFailure(std::move(status));
  }

 private:
  WeakRefCountedPtr<XdsChannel> xds_channel_;
};

//
// XdsClient::XdsChannel
//

XdsClient::XdsChannel::XdsChannel(WeakRefCountedPtr<XdsClient> xds_client,
                                  const XdsBootstrap::XdsServer& server)
    : DualRefCounted<XdsChannel>(GRPC_TRACE_FLAG_ENABLED(xds_client_refcount)
                                     ? "XdsChannel"
                                     : nullptr),
      xds_client_(std::move(xds_client)),
      server_(server) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client_.get() << "] creating channel " << this
      << " for server " << server_uri();
  absl::Status status;
  transport_ =
      xds_client_->transport_factory_->GetTransport(*server.target(), &status);
  CHECK(transport_ != nullptr);
  if (!status.ok()) {
    SetChannelStatusLocked(std::move(status));
  } else {
    failure_watcher_ = MakeRefCounted<ConnectivityFailureWatcher>(
        WeakRef(DEBUG_LOCATION, "OnConnectivityFailure"));
    transport_->StartConnectivityFailureWatch(failure_watcher_);
  }
}

XdsClient::XdsChannel::~XdsChannel() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] destroying xds channel " << this
      << " for server " << server_uri();
  xds_client_.reset(DEBUG_LOCATION, "XdsChannel");
}

// This method should only ever be called when holding the lock, but we can't
// use a ABSL_EXCLUSIVE_LOCKS_REQUIRED annotation, because Orphan() will be
// called from DualRefCounted::Unref, which cannot have a lock annotation for
// a lock in this subclass.
void XdsClient::XdsChannel::Orphaned() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] orphaning xds channel " << this
      << " for server " << server_uri();
  shutting_down_ = true;
  if (failure_watcher_ != nullptr) {
    transport_->StopConnectivityFailureWatch(failure_watcher_);
    failure_watcher_.reset();
  }
  transport_.reset();
  // At this time, all strong refs are removed, remove from channel map to
  // prevent subsequent subscription from trying to use this XdsChannel as
  // it is shutting down.
  xds_client_->xds_channel_map_.erase(server_.Key());
  ads_call_.reset();
}

void XdsClient::XdsChannel::ResetBackoff() { transport_->ResetBackoff(); }

void XdsClient::XdsChannel::SubscribeLocked(const XdsResourceType* type,
                                            const XdsResourceName& name) {
  if (ads_call_ == nullptr) {
    // Start the ADS call if this is the first request.
    ads_call_ = MakeOrphanable<RetryableCall<AdsCall>>(
        WeakRef(DEBUG_LOCATION, "XdsChannel+ads"));
    // Note: AdsCall's ctor will automatically subscribe to all
    // resources that the XdsClient already has watchers for, so we can
    // return here.
    return;
  }
  // If the ADS call is in backoff state, we don't need to do anything now
  // because when the call is restarted it will resend all necessary requests.
  if (ads_call_->call() == nullptr) return;
  // Subscribe to this resource if the ADS call is active.
  ads_call_->call()->SubscribeLocked(type, name, /*delay_send=*/false);
}

void XdsClient::XdsChannel::UnsubscribeLocked(const XdsResourceType* type,
                                              const XdsResourceName& name,
                                              bool delay_unsubscription) {
  if (ads_call_ != nullptr) {
    auto* call = ads_call_->call();
    if (call != nullptr) {
      call->UnsubscribeLocked(type, name, delay_unsubscription);
      if (!call->HasSubscribedResources()) {
        ads_call_.reset();
      }
    } else {
      // If there is currently no ADS call because we're in retry backoff,
      // then we immediately trigger deletion of unsubscribed cache entries.
      // This may orphan the XdsChannel, which would stop the retry
      // timer, since we would no longer need to restart the ADS call.
      xds_client_->MaybeRemoveUnsubscribedCacheEntriesForTypeLocked(this, type);
    }
  }
}

bool XdsClient::XdsChannel::MaybeFallbackLocked(
    const std::string& authority, AuthorityState& authority_state) {
  if (!xds_client_->HasUncachedResources(authority_state)) {
    return false;
  }
  std::vector<const XdsBootstrap::XdsServer*> xds_servers;
  if (authority != kOldStyleAuthority) {
    xds_servers =
        xds_client_->bootstrap().LookupAuthority(authority)->servers();
  }
  if (xds_servers.empty()) xds_servers = xds_client_->bootstrap().servers();
  for (size_t i = authority_state.xds_channels.size(); i < xds_servers.size();
       ++i) {
    authority_state.xds_channels.emplace_back(
        xds_client_->GetOrCreateXdsChannelLocked(*xds_servers[i], "fallback"));
    for (const auto& [type, resource_map] : authority_state.type_map) {
      for (const auto& [key, resource_state] : resource_map) {
        if (resource_state.HasWatchers()) {
          authority_state.xds_channels.back()->SubscribeLocked(
              type, {authority, key});
        }
      }
    }
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << xds_client_.get() << "] authority " << authority
        << ": added fallback server " << xds_servers[i]->target()->server_uri()
        << " (" << authority_state.xds_channels.back()->status().ToString()
        << ")";
    if (authority_state.xds_channels.back()->status().ok()) return true;
  }
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client_.get() << "] authority " << authority
      << ": No fallback server";
  return false;
}

void XdsClient::XdsChannel::SetHealthyLocked() {
  status_ = absl::OkStatus();
  // Make this channel active iff:
  // 1. Channel is on the list of authority channels
  // 2. Channel is not the last channel on the list (i.e. not the active
  // channel)
  for (auto& [authority, authority_state] : xds_client_->authority_state_map_) {
    auto& channels = authority_state.xds_channels;
    // Skip if channel is active.
    if (channels.back() == this) continue;
    auto channel_it = std::find(channels.begin(), channels.end(), this);
    // Skip if this is not on the list
    if (channel_it != channels.end()) {
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << xds_client_.get() << "] authority " << authority
          << ": Falling forward to " << server_uri();
      // Lower priority channels are no longer needed, connection is back!
      // Note that we move the lower priority channels out of the vector
      // before we unref them, or else
      // MaybeRemoveUnsubscribedCacheEntriesForTypeLocked() will try to
      // access the vector while we are modifying it.
      std::vector<RefCountedPtr<XdsChannel>> channels_to_unref(
          std::make_move_iterator(channel_it + 1),
          std::make_move_iterator(channels.end()));
      channels.erase(channel_it + 1, channels.end());
    }
  }
}

void XdsClient::XdsChannel::OnConnectivityFailure(absl::Status status) {
  MutexLock lock(&xds_client_->mu_);
  SetChannelStatusLocked(std::move(status));
}

void XdsClient::XdsChannel::SetChannelStatusLocked(absl::Status status) {
  if (shutting_down_) return;
  status = absl::Status(
      status.code(), absl::StrCat("xDS channel for server ", server_uri(), ": ",
                                  status.message()));
  LOG(INFO) << "[xds_client " << xds_client() << "] " << status;
  // If status was previously OK, report that the channel has gone unhealthy.
  if (status_.ok() && xds_client_->metrics_reporter_ != nullptr) {
    xds_client_->metrics_reporter_->ReportServerFailure(server_uri());
  }
  // Save status in channel, so that we can immediately generate an
  // error for any new watchers that may be started.
  status_ = status;
  // Find all watchers for this channel.
  WatcherSet watchers_cached;
  WatcherSet watchers_uncached;
  for (auto& [authority, authority_state] : xds_client_->authority_state_map_) {
    if (authority_state.xds_channels.empty() ||
        authority_state.xds_channels.back() != this ||
        MaybeFallbackLocked(authority, authority_state)) {
      continue;
    }
    for (const auto& [_, resource_map] : authority_state.type_map) {
      for (const auto& [_, resource_state] : resource_map) {
        auto& watchers =
            resource_state.HasResource() ? watchers_cached : watchers_uncached;
        for (const auto& watcher : resource_state.watchers()) {
          watchers.insert(watcher);
        }
      }
    }
  }
  // Enqueue notifications for the watchers.
  if (!watchers_cached.empty()) {
    xds_client_->NotifyWatchersOnAmbientError(
        status, std::move(watchers_cached), ReadDelayHandle::NoWait());
  }
  if (!watchers_uncached.empty()) {
    xds_client_->NotifyWatchersOnResourceChanged(
        status, std::move(watchers_uncached), ReadDelayHandle::NoWait());
  }
}

//
// XdsClient::XdsChannel::RetryableCall<>
//

template <typename T>
XdsClient::XdsChannel::RetryableCall<T>::RetryableCall(
    WeakRefCountedPtr<XdsChannel> xds_channel)
    : xds_channel_(std::move(xds_channel)),
      backoff_(BackOff::Options()
                   .set_initial_backoff(Duration::Seconds(
                       GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS))
                   .set_multiplier(GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER)
                   .set_jitter(GRPC_XDS_RECONNECT_JITTER)
                   .set_max_backoff(Duration::Seconds(
                       GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS))) {
  StartNewCallLocked();
}

template <typename T>
void XdsClient::XdsChannel::RetryableCall<T>::Orphan() {
  shutting_down_ = true;
  call_.reset();
  if (timer_handle_.has_value()) {
    xds_channel()->xds_client()->engine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  this->Unref(DEBUG_LOCATION, "RetryableCall+orphaned");
}

template <typename T>
void XdsClient::XdsChannel::RetryableCall<T>::OnCallFinishedLocked() {
  // If we saw a response on the current stream, reset backoff.
  if (call_->seen_response()) backoff_.Reset();
  call_.reset();
  // Start retry timer.
  StartRetryTimerLocked();
}

template <typename T>
void XdsClient::XdsChannel::RetryableCall<T>::StartNewCallLocked() {
  if (shutting_down_) return;
  CHECK(xds_channel_->transport_ != nullptr);
  CHECK(call_ == nullptr);
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_channel()->xds_client() << "] xds server "
      << xds_channel()->server_uri() << ": start new call from retryable call "
      << this;
  call_ = MakeOrphanable<T>(
      this->Ref(DEBUG_LOCATION, "RetryableCall+start_new_call"));
}

template <typename T>
void XdsClient::XdsChannel::RetryableCall<T>::StartRetryTimerLocked() {
  if (shutting_down_) return;
  const Duration delay = backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_channel()->xds_client() << "] xds server "
      << xds_channel()->server_uri()
      << ": call attempt failed; retry timer will fire in " << delay.millis()
      << "ms.";
  timer_handle_ = xds_channel()->xds_client()->engine()->RunAfter(
      delay,
      [self = this->Ref(DEBUG_LOCATION, "RetryableCall+retry_timer_start")]() {
        ExecCtx exec_ctx;
        self->OnRetryTimer();
      });
}

template <typename T>
void XdsClient::XdsChannel::RetryableCall<T>::OnRetryTimer() {
  MutexLock lock(&xds_channel_->xds_client()->mu_);
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    if (shutting_down_) return;
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << xds_channel()->xds_client() << "] xds server "
        << xds_channel()->server_uri()
        << ": retry timer fired (retryable call: " << this << ")";
    StartNewCallLocked();
  }
}

//
// XdsClient::XdsChannel::AdsCall::AdsReadDelayHandle
//

class XdsClient::XdsChannel::AdsCall::AdsReadDelayHandle final
    : public XdsClient::ReadDelayHandle {
 public:
  explicit AdsReadDelayHandle(RefCountedPtr<AdsCall> ads_call)
      : ads_call_(std::move(ads_call)) {}

  ~AdsReadDelayHandle() override {
    MutexLock lock(&ads_call_->xds_client()->mu_);
    auto call = ads_call_->streaming_call_.get();
    if (call != nullptr) call->StartRecvMessage();
  }

 private:
  RefCountedPtr<AdsCall> ads_call_;
};

//
// XdsClient::XdsChannel::AdsCall
//

XdsClient::XdsChannel::AdsCall::AdsCall(
    RefCountedPtr<RetryableCall<AdsCall>> retryable_call)
    : InternallyRefCounted<AdsCall>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "AdsCall" : nullptr),
      retryable_call_(std::move(retryable_call)) {
  CHECK_NE(xds_client(), nullptr);
  // Init the ADS call.
  const char* method =
      "/envoy.service.discovery.v3.AggregatedDiscoveryService/"
      "StreamAggregatedResources";
  streaming_call_ = xds_channel()->transport_->CreateStreamingCall(
      method, std::make_unique<StreamEventHandler>(
                  // Passing the initial ref here.  This ref will go away when
                  // the StreamEventHandler is destroyed.
                  RefCountedPtr<AdsCall>(this)));
  CHECK(streaming_call_ != nullptr);
  // Start the call.
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] xds server "
      << xds_channel()->server_uri()
      << ": starting ADS call (ads_call: " << this
      << ", streaming_call: " << streaming_call_.get() << ")";
  // If this is a reconnect, add any necessary subscriptions from what's
  // already in the cache.
  for (auto& [authority, authority_state] :
       xds_client()->authority_state_map_) {
    auto it = std::find(authority_state.xds_channels.begin(),
                        authority_state.xds_channels.end(), xds_channel());
    // Skip authorities that are not using this xDS channel. The channel can be
    // anywhere in the list.
    if (it == authority_state.xds_channels.end()) continue;
    for (const auto& [type, resource_map] : authority_state.type_map) {
      for (const auto& [resource_key, resource_state] : resource_map) {
        if (resource_state.HasWatchers()) {
          SubscribeLocked(type, {authority, resource_key}, /*delay_send=*/true);
        }
      }
    }
  }
  // Send initial message if we added any subscriptions above.
  for (const auto& [type, _] : state_map_) {
    SendMessageLocked(type);
  }
  streaming_call_->StartRecvMessage();
}

void XdsClient::XdsChannel::AdsCall::Orphan() {
  state_map_.clear();
  // We may have unsubscriptions for which we have not yet actually sent
  // unsubscribe messages, and now we never will, so do a pass to delete
  // any cache entries for which we've unsubscribed.
  for (const auto& [_, type] : xds_client()->resource_types_) {
    xds_client()->MaybeRemoveUnsubscribedCacheEntriesForTypeLocked(
        xds_channel(), type);
  }
  // Note that the initial ref is held by the StreamEventHandler, which
  // will be destroyed when streaming_call_ is destroyed, which may not happen
  // here, since there may be other refs held to streaming_call_ by internal
  // callbacks.
  streaming_call_.reset();
}

void XdsClient::XdsChannel::AdsCall::SubscribeLocked(
    const XdsResourceType* type, const XdsResourceName& name, bool delay_send) {
  auto& state = state_map_[type].subscribed_resources[name.authority][name.key];
  if (state == nullptr) {
    state = MakeOrphanable<ResourceTimer>(type, name);
    if (!delay_send) SendMessageLocked(type);
  }
}

void XdsClient::XdsChannel::AdsCall::UnsubscribeLocked(
    const XdsResourceType* type, const XdsResourceName& name,
    bool delay_unsubscription) {
  auto& type_state_map = state_map_[type];
  auto& authority_map = type_state_map.subscribed_resources[name.authority];
  authority_map.erase(name.key);
  if (authority_map.empty()) {
    type_state_map.subscribed_resources.erase(name.authority);
    // Note: We intentionally do not remove the top-level map entry for
    // the resource type even if the authority map for the type is empty,
    // because we need to retain the nonce in case a new watch is
    // started for a resource of this type while this stream is still open.
  }
  // Don't need to send unsubscription message if this was the last
  // resource we were subscribed to, since we'll be closing the stream
  // immediately in that case.
  if (!delay_unsubscription && HasSubscribedResources()) {
    SendMessageLocked(type);
  }
}

bool XdsClient::XdsChannel::AdsCall::HasSubscribedResources() const {
  for (const auto& [_, resource_type_state] : state_map_) {
    if (!resource_type_state.subscribed_resources.empty()) return true;
  }
  return false;
}

namespace {

void MaybeLogDiscoveryRequest(
    const XdsClient* client, upb_DefPool* def_pool,
    const envoy_service_discovery_v3_DiscoveryRequest* request) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_client) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_discovery_v3_DiscoveryRequest_getmsgdef(def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(request), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << client << "] constructed ADS request: " << buf;
  }
}

std::string SerializeDiscoveryRequest(
    upb_Arena* arena, envoy_service_discovery_v3_DiscoveryRequest* request) {
  size_t output_length;
  char* output = envoy_service_discovery_v3_DiscoveryRequest_serialize(
      request, arena, &output_length);
  return std::string(output, output_length);
}

}  // namespace

std::string XdsClient::XdsChannel::AdsCall::CreateAdsRequest(
    absl::string_view type_url, absl::string_view version,
    absl::string_view nonce, const std::vector<std::string>& resource_names,
    absl::Status status) const {
  upb::Arena arena;
  // Create a request.
  envoy_service_discovery_v3_DiscoveryRequest* request =
      envoy_service_discovery_v3_DiscoveryRequest_new(arena.ptr());
  // Set type_url.
  std::string type_url_str = absl::StrCat("type.googleapis.com/", type_url);
  envoy_service_discovery_v3_DiscoveryRequest_set_type_url(
      request, StdStringToUpbString(type_url_str));
  // Set version_info.
  if (!version.empty()) {
    envoy_service_discovery_v3_DiscoveryRequest_set_version_info(
        request, StdStringToUpbString(version));
  }
  // Set nonce.
  if (!nonce.empty()) {
    envoy_service_discovery_v3_DiscoveryRequest_set_response_nonce(
        request, StdStringToUpbString(nonce));
  }
  // Set error_detail if it's a NACK.
  std::string error_string_storage;
  if (!status.ok()) {
    google_rpc_Status* error_detail =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_error_detail(
            request, arena.ptr());
    // Hard-code INVALID_ARGUMENT as the status code.
    // TODO(roth): If at some point we decide we care about this value,
    // we could attach a status code to the individual errors where we
    // generate them in the parsing code, and then use that here.
    google_rpc_Status_set_code(error_detail, GRPC_STATUS_INVALID_ARGUMENT);
    // Error description comes from the status that was passed in.
    error_string_storage = std::string(status.message());
    upb_StringView error_description =
        StdStringToUpbString(error_string_storage);
    google_rpc_Status_set_message(error_detail, error_description);
  }
  // Populate node.
  if (!sent_initial_message_) {
    envoy_config_core_v3_Node* node_msg =
        envoy_service_discovery_v3_DiscoveryRequest_mutable_node(request,
                                                                 arena.ptr());
    PopulateXdsNode(xds_client()->bootstrap_->node(),
                    xds_client()->user_agent_name_,
                    xds_client()->user_agent_version_, node_msg, arena.ptr());
    envoy_config_core_v3_Node_add_client_features(
        node_msg, upb_StringView_FromString("xds.config.resource-in-sotw"),
        arena.ptr());
  }
  // Add resource_names.
  for (const std::string& resource_name : resource_names) {
    envoy_service_discovery_v3_DiscoveryRequest_add_resource_names(
        request, StdStringToUpbString(resource_name), arena.ptr());
  }
  MaybeLogDiscoveryRequest(xds_client(), xds_client()->def_pool_.ptr(),
                           request);
  return SerializeDiscoveryRequest(arena.ptr(), request);
}

void XdsClient::XdsChannel::AdsCall::SendMessageLocked(
    const XdsResourceType* type)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
  // Buffer message sending if an existing message is in flight.
  if (send_message_pending_ != nullptr) {
    buffered_requests_.insert(type);
    return;
  }
  xds_client()->MaybeRemoveUnsubscribedCacheEntriesForTypeLocked(xds_channel(),
                                                                 type);
  auto& state = state_map_[type];
  std::string serialized_message = CreateAdsRequest(
      type->type_url(), xds_channel()->resource_type_version_map_[type],
      state.nonce, ResourceNamesForRequest(type), state.status);
  sent_initial_message_ = true;
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] xds server "
      << xds_channel()->server_uri()
      << ": sending ADS request: type=" << type->type_url()
      << " version=" << xds_channel()->resource_type_version_map_[type]
      << " nonce=" << state.nonce << " error=" << state.status;
  state.status = absl::OkStatus();
  streaming_call_->SendMessage(std::move(serialized_message));
  send_message_pending_ = type;
}

void XdsClient::XdsChannel::AdsCall::OnRequestSent(bool ok) {
  MutexLock lock(&xds_client()->mu_);
  // For each resource that was in the message we just sent, start the
  // resource timer if needed.
  if (ok) {
    auto& resource_type_state = state_map_[send_message_pending_];
    for (const auto& [_, resource_map] :
         resource_type_state.subscribed_resources) {
      for (auto& [_, resource_timer] : resource_map) {
        resource_timer->MaybeMarkSubscriptionSendComplete(
            Ref(DEBUG_LOCATION, "ResourceTimer"));
      }
    }
  }
  send_message_pending_ = nullptr;
  if (ok && IsCurrentCallOnChannel()) {
    // Continue to send another pending message if any.
    // TODO(roth): The current code to handle buffered messages has the
    // advantage of sending only the most recent list of resource names for
    // each resource type (no matter how many times that resource type has
    // been requested to send while the current message sending is still
    // pending). But its disadvantage is that we send the requests in fixed
    // order of resource types. We need to fix this if we are seeing some
    // resource type(s) starved due to frequent requests of other resource
    // type(s).
    auto it = buffered_requests_.begin();
    if (it != buffered_requests_.end()) {
      SendMessageLocked(*it);
      buffered_requests_.erase(it);
    }
  }
}

void XdsClient::XdsChannel::AdsCall::ParseResource(
    size_t idx, absl::string_view type_url, absl::string_view resource_name,
    absl::string_view serialized_resource, DecodeContext* context) {
  std::string error_prefix = absl::StrCat(
      "resource index ", idx, ": ",
      resource_name.empty() ? "" : absl::StrCat(resource_name, ": "));
  // Check the type_url of the resource.
  if (context->type_url != type_url) {
    context->errors.emplace_back(
        absl::StrCat(error_prefix, "incorrect resource type \"", type_url,
                     "\" (should be \"", context->type_url, "\")"));
    ++context->num_invalid_resources;
    return;
  }
  // Parse the resource.
  XdsResourceType::DecodeContext resource_type_context = {
      xds_client(), xds_channel()->server_, xds_client()->def_pool_.ptr(),
      context->arena.ptr()};
  XdsResourceType::DecodeResult decode_result =
      context->type->Decode(resource_type_context, serialized_resource);
  // If we didn't already have the resource name from the Resource
  // wrapper, try to get it from the decoding result.
  if (resource_name.empty()) {
    if (decode_result.name.has_value()) {
      resource_name = *decode_result.name;
      error_prefix =
          absl::StrCat("resource index ", idx, ": ", resource_name, ": ");
    } else {
      // We don't have any way of determining the resource name, so
      // there's nothing more we can do here.
      context->errors.emplace_back(absl::StrCat(
          error_prefix, decode_result.resource.status().ToString()));
      ++context->num_invalid_resources;
      return;
    }
  }
  // If decoding failed, make sure we include the error in the NACK.
  const absl::Status& decode_status = decode_result.resource.status();
  if (!decode_status.ok()) {
    context->errors.emplace_back(
        absl::StrCat(error_prefix, decode_status.ToString()));
  }
  // Check the resource name.
  auto parsed_resource_name =
      xds_client()->ParseXdsResourceName(resource_name, context->type);
  if (!parsed_resource_name.ok()) {
    context->errors.emplace_back(
        absl::StrCat(error_prefix, "Cannot parse xDS resource name"));
    ++context->num_invalid_resources;
    return;
  }
  // Cancel resource-does-not-exist timer, if needed.
  if (auto it = state_map_.find(context->type); it != state_map_.end()) {
    auto& resource_type_state = it->second;
    auto authority_it = resource_type_state.subscribed_resources.find(
        parsed_resource_name->authority);
    if (authority_it != resource_type_state.subscribed_resources.end()) {
      auto& resource_map = authority_it->second;
      auto res_it = resource_map.find(parsed_resource_name->key);
      if (res_it != resource_map.end()) {
        res_it->second->MarkSeen();
      }
    }
  }
  // Lookup the authority in the cache.
  auto authority_it =
      xds_client()->authority_state_map_.find(parsed_resource_name->authority);
  if (authority_it == xds_client()->authority_state_map_.end()) {
    return;  // Skip resource -- we don't have a subscription for it.
  }
  AuthorityState& authority_state = authority_it->second;
  // Found authority, so look up type.
  auto type_it = authority_state.type_map.find(context->type);
  if (type_it == authority_state.type_map.end()) {
    return;  // Skip resource -- we don't have a subscription for it.
  }
  auto& type_map = type_it->second;
  // Found type, so look up resource key.
  auto res_it = type_map.find(parsed_resource_name->key);
  if (res_it == type_map.end()) {
    return;  // Skip resource -- we don't have a subscription for it.
  }
  ResourceState& resource_state = res_it->second;
  // If needed, record that we've seen this resource.
  if (context->type->AllResourcesRequiredInSotW()) {
    context->resources_seen[parsed_resource_name->authority].insert(
        parsed_resource_name->key);
  }
  // Update resource state based on whether the resource is valid.
  if (!decode_status.ok()) {
    ++context->num_invalid_resources;
    // If the fail_on_data_errors server feature is present, drop the
    // existing cached resource, if any.
    const bool drop_cached_resource = XdsDataErrorHandlingEnabled() &&
                                      xds_channel()->server_.FailOnDataErrors();
    resource_state.SetNacked(context->version, decode_status.message(),
                             context->update_time, drop_cached_resource);
    xds_client()->NotifyWatchersOnError(resource_state,
                                        context->read_delay_handle);
    return;
  }
  // Resource is valid.
  ++context->num_valid_resources;
  // Check if the resource has changed.
  const bool resource_identical =
      resource_state.HasResource() &&
      context->type->ResourcesEqual(resource_state.resource().get(),
                                    decode_result.resource->get());
  // If not changed, keep using the current decoded resource object.
  // This should avoid wasting memory, since external watchers may be
  // holding refs to the current object.
  if (resource_identical) decode_result.resource = resource_state.resource();
  // Update the resource state.
  resource_state.SetAcked(std::move(*decode_result.resource),
                          std::string(serialized_resource), context->version,
                          context->update_time);
  // If the resource didn't change, inhibit watcher notifications.
  if (resource_identical) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << xds_client() << "] " << context->type_url
        << " resource " << resource_name << " identical to current, ignoring.";
    // If we previously had connectivity problems, notify watchers that
    // the ambient error has been cleared.
    if (!xds_channel()->status().ok()) {
      xds_client()->NotifyWatchersOnAmbientError(absl::OkStatus(),
                                                 resource_state.watchers(),
                                                 context->read_delay_handle);
    }
    return;
  }
  // Notify watchers.
  xds_client()->NotifyWatchersOnResourceChanged(resource_state.resource(),
                                                resource_state.watchers(),
                                                context->read_delay_handle);
}

void XdsClient::XdsChannel::AdsCall::HandleServerReportedResourceError(
    size_t idx, absl::string_view resource_name, absl::Status status,
    DecodeContext* context) {
  std::string error_prefix = absl::StrCat(
      "resource_errors index ", idx, ": ",
      resource_name.empty() ? "" : absl::StrCat(resource_name, ": "));
  if (resource_name.empty()) {
    context->errors.emplace_back(
        absl::StrCat(error_prefix, "resource_name unset"));
    ++context->num_invalid_resources;
    return;
  }
  if (status.ok()) {
    context->errors.emplace_back(
        absl::StrCat(error_prefix, "error_detail must be non-OK"));
    ++context->num_invalid_resources;
    return;
  }
  // Check the resource name.
  auto parsed_resource_name =
      xds_client()->ParseXdsResourceName(resource_name, context->type);
  if (!parsed_resource_name.ok()) {
    context->errors.emplace_back(
        absl::StrCat(error_prefix, "Cannot parse xDS resource name"));
    ++context->num_invalid_resources;
    return;
  }
  // Cancel resource-does-not-exist timer, if needed.
  auto timer_it = state_map_.find(context->type);
  if (timer_it != state_map_.end()) {
    auto it = timer_it->second.subscribed_resources.find(
        parsed_resource_name->authority);
    if (it != timer_it->second.subscribed_resources.end()) {
      auto res_it = it->second.find(parsed_resource_name->key);
      if (res_it != it->second.end()) {
        res_it->second->MarkSeen();
      }
    }
  }
  // Lookup the authority in the cache.
  auto authority_it =
      xds_client()->authority_state_map_.find(parsed_resource_name->authority);
  if (authority_it == xds_client()->authority_state_map_.end()) {
    return;  // Skip resource -- we don't have a subscription for it.
  }
  AuthorityState& authority_state = authority_it->second;
  // Found authority, so look up type.
  auto type_it = authority_state.type_map.find(context->type);
  if (type_it == authority_state.type_map.end()) {
    return;  // Skip resource -- we don't have a subscription for it.
  }
  auto& type_map = type_it->second;
  // Found type, so look up resource key.
  auto it = type_map.find(parsed_resource_name->key);
  if (it == type_map.end()) {
    return;  // Skip resource -- we don't have a subscription for it.
  }
  ResourceState& resource_state = it->second;
  // If needed, record that we've seen this resource.
  if (context->type->AllResourcesRequiredInSotW()) {
    context->resources_seen[parsed_resource_name->authority].insert(
        parsed_resource_name->key);
  }
  ++context->num_invalid_resources;
  // Update cache state.
  const bool drop_cached_resource =
      xds_channel()->server_.FailOnDataErrors() &&
      (status.code() == absl::StatusCode::kNotFound ||
       status.code() == absl::StatusCode::kPermissionDenied);
  resource_state.SetReceivedError(context->version, std::move(status),
                                  context->update_time, drop_cached_resource);
  // If there is no cached resource (either because we didn't have one
  // or because we just dropped it due to fail_on_data_errors), then notify
  // via OnResourceChanged(); otherwise, notify via OnAmbientError().
  if (!resource_state.HasResource()) {
    xds_client()->NotifyWatchersOnResourceChanged(
        resource_state.failed_status(), resource_state.watchers(),
        context->read_delay_handle);
  } else {
    xds_client()->NotifyWatchersOnAmbientError(resource_state.failed_status(),
                                               resource_state.watchers(),
                                               context->read_delay_handle);
  }
}

namespace {

void MaybeLogDiscoveryResponse(
    const XdsClient* client, upb_DefPool* def_pool,
    const envoy_service_discovery_v3_DiscoveryResponse* response) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_client) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_service_discovery_v3_DiscoveryResponse_getmsgdef(def_pool);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(response), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << client << "] received response: " << buf;
  }
}

}  // namespace

absl::Status XdsClient::XdsChannel::AdsCall::DecodeAdsResponse(
    absl::string_view encoded_response, DecodeContext* context) {
  // Decode the response.
  const envoy_service_discovery_v3_DiscoveryResponse* response =
      envoy_service_discovery_v3_DiscoveryResponse_parse(
          encoded_response.data(), encoded_response.size(),
          context->arena.ptr());
  // If decoding fails, report a fatal error and return.
  if (response == nullptr) {
    return absl::InvalidArgumentError("Can't decode DiscoveryResponse.");
  }
  MaybeLogDiscoveryResponse(xds_client(), xds_client()->def_pool_.ptr(),
                            response);
  // Get the type_url, version, nonce, number of resources, and number
  // of errors.
  context->type_url = std::string(absl::StripPrefix(
      UpbStringToAbsl(
          envoy_service_discovery_v3_DiscoveryResponse_type_url(response)),
      "type.googleapis.com/"));
  context->version = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_version_info(response));
  context->nonce = UpbStringToStdString(
      envoy_service_discovery_v3_DiscoveryResponse_nonce(response));
  size_t num_resources;
  const google_protobuf_Any* const* resources =
      envoy_service_discovery_v3_DiscoveryResponse_resources(response,
                                                             &num_resources);
  size_t num_errors = 0;
  const envoy_service_discovery_v3_ResourceError* const* errors = nullptr;
  if (XdsDataErrorHandlingEnabled()) {
    errors = envoy_service_discovery_v3_DiscoveryResponse_resource_errors(
        response, &num_errors);
  }
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] xds server "
      << xds_channel()->server_uri()
      << ": received ADS response: type_url=" << context->type_url
      << ", version=" << context->version << ", nonce=" << context->nonce
      << ", num_resources=" << num_resources << ", num_errors=" << num_errors;
  context->type = xds_client()->GetResourceTypeLocked(context->type_url);
  if (context->type == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown resource type ", context->type_url));
  }
  context->read_delay_handle = MakeRefCounted<AdsReadDelayHandle>(Ref());
  // Process each resource.
  for (size_t i = 0; i < num_resources; ++i) {
    absl::string_view type_url = absl::StripPrefix(
        UpbStringToAbsl(google_protobuf_Any_type_url(resources[i])),
        "type.googleapis.com/");
    absl::string_view serialized_resource =
        UpbStringToAbsl(google_protobuf_Any_value(resources[i]));
    // Unwrap Resource messages, if so wrapped.
    absl::string_view resource_name;
    if (type_url == "envoy.service.discovery.v3.Resource") {
      const auto* resource_wrapper = envoy_service_discovery_v3_Resource_parse(
          serialized_resource.data(), serialized_resource.size(),
          context->arena.ptr());
      if (resource_wrapper == nullptr) {
        context->errors.emplace_back(absl::StrCat(
            "resource index ", i, ": Can't decode Resource proto wrapper"));
        ++context->num_invalid_resources;
        continue;
      }
      const auto* resource =
          envoy_service_discovery_v3_Resource_resource(resource_wrapper);
      if (resource == nullptr) {
        context->errors.emplace_back(
            absl::StrCat("resource index ", i,
                         ": No resource present in Resource proto wrappe"));
        ++context->num_invalid_resources;
        continue;
      }
      type_url = absl::StripPrefix(
          UpbStringToAbsl(google_protobuf_Any_type_url(resource)),
          "type.googleapis.com/");
      serialized_resource =
          UpbStringToAbsl(google_protobuf_Any_value(resource));
      resource_name = UpbStringToAbsl(
          envoy_service_discovery_v3_Resource_name(resource_wrapper));
    }
    ParseResource(i, type_url, resource_name, serialized_resource, context);
  }
  // Process each error.
  for (size_t i = 0; i < num_errors; ++i) {
    absl::string_view name;
    {
      const envoy_service_discovery_v3_ResourceName* resource_name =
          envoy_service_discovery_v3_ResourceError_resource_name(errors[i]);
      if (resource_name != nullptr) {
        name = UpbStringToAbsl(
            envoy_service_discovery_v3_ResourceName_name(resource_name));
      }
    }
    absl::Status status;
    {
      const google_rpc_Status* error_detail =
          envoy_service_discovery_v3_ResourceError_error_detail(errors[i]);
      if (error_detail != nullptr) {
        status = absl::Status(
            static_cast<absl::StatusCode>(google_rpc_Status_code(error_detail)),
            UpbStringToAbsl(google_rpc_Status_message(error_detail)));
      }
    }
    HandleServerReportedResourceError(i, name, std::move(status), context);
  }
  return absl::OkStatus();
}

void XdsClient::XdsChannel::AdsCall::OnRecvMessage(absl::string_view payload) {
  // context.read_delay_handle needs to be destroyed after the mutex is
  // released.
  DecodeContext context;
  MutexLock lock(&xds_client()->mu_);
  if (!IsCurrentCallOnChannel()) return;
  // Parse and validate the response.
  absl::Status status = DecodeAdsResponse(payload, &context);
  if (!status.ok()) {
    // Ignore unparsable response.
    LOG(ERROR) << "[xds_client " << xds_client() << "] xds server "
               << xds_channel()->server_uri()
               << ": error parsing ADS response (" << status << ") -- ignoring";
  } else {
    seen_response_ = true;
    xds_channel()->SetHealthyLocked();
    // Update nonce.
    auto& state = state_map_[context.type];
    state.nonce = context.nonce;
    // If we got an error, set state.status so that we'll NACK the update.
    if (!context.errors.empty()) {
      state.status = absl::UnavailableError(
          absl::StrCat("xDS response validation errors: [",
                       absl::StrJoin(context.errors, "; "), "]"));
      LOG(ERROR) << "[xds_client " << xds_client() << "] xds server "
                 << xds_channel()->server_uri()
                 << ": ADS response invalid for resource type "
                 << context.type_url << " version " << context.version
                 << ", will NACK: nonce=" << state.nonce
                 << " status=" << state.status;
    }
    // Delete resources not seen in update if needed.
    if (context.type->AllResourcesRequiredInSotW()) {
      for (auto& [authority, authority_state] :
           xds_client()->authority_state_map_) {
        // Skip authorities that are not using this xDS channel.
        if (authority_state.xds_channels.back() != xds_channel()) {
          continue;
        }
        auto seen_authority_it = context.resources_seen.find(authority);
        // Find this resource type.
        auto type_it = authority_state.type_map.find(context.type);
        if (type_it == authority_state.type_map.end()) continue;
        // Iterate over resource ids.
        for (auto& [resource_key, resource_state] : type_it->second) {
          if (seen_authority_it == context.resources_seen.end() ||
              seen_authority_it->second.find(resource_key) ==
                  seen_authority_it->second.end()) {
            // If the resource was newly requested but has not yet been
            // received, we don't want to generate an error for the
            // watchers, because this ADS response may be in reaction to an
            // earlier request that did not yet request the new resource, so
            // its absence from the response does not necessarily indicate
            // that the resource does not exist.  For that case, we rely on
            // the request timeout instead.
            if (!resource_state.HasResource()) continue;
            const bool drop_cached_resource =
                XdsDataErrorHandlingEnabled()
                    ? xds_channel()->server_.FailOnDataErrors()
                    : !xds_channel()->server_.IgnoreResourceDeletion();
            resource_state.SetDoesNotExistOnLdsOrCdsDeletion(
                context.version, context.update_time, drop_cached_resource);
            xds_client()->NotifyWatchersOnError(resource_state,
                                                context.read_delay_handle);
          }
        }
      }
    }
    // If we had valid resources or the update was empty, update the version.
    if (context.num_valid_resources > 0 || context.errors.empty()) {
      xds_channel()->resource_type_version_map_[context.type] =
          std::move(context.version);
    }
    // Send ACK or NACK.
    SendMessageLocked(context.type);
  }
  // Update metrics.
  if (xds_client()->metrics_reporter_ != nullptr) {
    xds_client()->metrics_reporter_->ReportResourceUpdates(
        xds_channel()->server_uri(), context.type_url,
        context.num_valid_resources, context.num_invalid_resources);
  }
}

void XdsClient::XdsChannel::AdsCall::OnStatusReceived(absl::Status status) {
  MutexLock lock(&xds_client()->mu_);
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] xds server "
      << xds_channel()->server_uri()
      << ": ADS call status received (xds_channel=" << xds_channel()
      << ", ads_call=" << this << ", streaming_call=" << streaming_call_.get()
      << "): " << status;
  // Cancel any does-not-exist timers that may be pending.
  for (const auto& [_, resource_type_state] : state_map_) {
    for (const auto& [_, resource_map] :
         resource_type_state.subscribed_resources) {
      for (auto& [_, resource_timer] : resource_map) {
        resource_timer->MaybeCancelTimer();
      }
    }
  }
  // Ignore status from a stale call.
  if (IsCurrentCallOnChannel()) {
    // Try to restart the call.
    retryable_call_->OnCallFinishedLocked();
    // If we didn't receive a response on the stream, report the
    // stream failure as a connectivity failure, which will report the
    // error to all watchers of resources on this channel.
    if (!seen_response_) {
      xds_channel()->SetChannelStatusLocked(absl::UnavailableError(
          absl::StrCat("xDS call failed with no responses received; status: ",
                       status.ToString())));
    }
  }
}

bool XdsClient::XdsChannel::AdsCall::IsCurrentCallOnChannel() const {
  // If the retryable ADS call is null (which only happens when the xds
  // channel is shutting down), all the ADS calls are stale.
  if (xds_channel()->ads_call_ == nullptr) return false;
  return this == xds_channel()->ads_call_->call();
}

std::vector<std::string>
XdsClient::XdsChannel::AdsCall::ResourceNamesForRequest(
    const XdsResourceType* type) {
  std::vector<std::string> resource_names;
  auto it = state_map_.find(type);
  if (it != state_map_.end()) {
    for (auto& [authority, authority_state] : it->second.subscribed_resources) {
      for (auto& [resource_key, resource_timer] : authority_state) {
        resource_names.emplace_back(XdsClient::ConstructFullXdsResourceName(
            authority, type->type_url(), resource_key));
        resource_timer->MarkSubscriptionSendStarted();
      }
    }
  }
  return resource_names;
}

//
// XdsClient::ResourceState
//

void XdsClient::ResourceState::SetAcked(
    std::shared_ptr<const XdsResourceType::ResourceData> resource,
    std::string serialized_proto, std::string version, Timestamp update_time) {
  resource_ = std::move(resource);
  client_status_ = ClientResourceStatus::ACKED;
  serialized_proto_ = std::move(serialized_proto);
  update_time_ = update_time;
  version_ = std::move(version);
  failed_version_.clear();
  failed_status_ = absl::OkStatus();
}

void XdsClient::ResourceState::SetNacked(const std::string& version,
                                         absl::string_view details,
                                         Timestamp update_time,
                                         bool drop_cached_resource) {
  if (drop_cached_resource) {
    resource_.reset();
    serialized_proto_.clear();
  }
  client_status_ = ClientResourceStatus::NACKED;
  failed_status_ =
      absl::InvalidArgumentError(absl::StrCat("invalid resource: ", details));
  failed_version_ = version;
  failed_update_time_ = update_time;
}

void XdsClient::ResourceState::SetReceivedError(const std::string& version,
                                                absl::Status status,
                                                Timestamp update_time,
                                                bool drop_cached_resource) {
  if (drop_cached_resource) {
    resource_.reset();
    serialized_proto_.clear();
  }
  client_status_ = ClientResourceStatus::RECEIVED_ERROR;
  failed_version_ = version;
  failed_status_ = std::move(status);
  failed_update_time_ = update_time;
}

void XdsClient::ResourceState::SetDoesNotExistOnLdsOrCdsDeletion(
    const std::string& version, Timestamp update_time,
    bool drop_cached_resource) {
  if (drop_cached_resource) {
    resource_.reset();
    serialized_proto_.clear();
  }
  client_status_ = ClientResourceStatus::DOES_NOT_EXIST;
  failed_status_ = absl::NotFoundError("does not exist");
  failed_version_ = version;
  failed_update_time_ = update_time;
}

void XdsClient::ResourceState::SetDoesNotExistOnTimeout() {
  client_status_ = ClientResourceStatus::DOES_NOT_EXIST;
  failed_status_ = absl::NotFoundError("does not exist");
  failed_version_.clear();
}

void XdsClient::ResourceState::SetTimeout(const std::string& details) {
  client_status_ = ClientResourceStatus::TIMEOUT;
  failed_status_ = absl::UnavailableError(details);
  failed_version_.clear();
}

absl::string_view XdsClient::ResourceState::CacheStateString() const {
  switch (client_status_) {
    case ClientResourceStatus::REQUESTED:
      return "requested";
    case ClientResourceStatus::DOES_NOT_EXIST:
      return resource_ != nullptr ? "does_not_exist_but_cached"
                                  : "does_not_exist";
    case ClientResourceStatus::ACKED:
      return "acked";
    case ClientResourceStatus::NACKED:
      return resource_ != nullptr ? "nacked_but_cached" : "nacked";
    case ClientResourceStatus::RECEIVED_ERROR:
      return resource_ != nullptr ? "received_error_but_cached"
                                  : "received_error";
    case ClientResourceStatus::TIMEOUT:
      return "timeout";
  }
  Crash("unknown resource state");
}

namespace {

google_protobuf_Timestamp* EncodeTimestamp(Timestamp value, upb_Arena* arena) {
  google_protobuf_Timestamp* timestamp = google_protobuf_Timestamp_new(arena);
  gpr_timespec timespec = value.as_timespec(GPR_CLOCK_REALTIME);
  google_protobuf_Timestamp_set_seconds(timestamp, timespec.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, timespec.tv_nsec);
  return timestamp;
}

}  // namespace

void XdsClient::ResourceState::FillGenericXdsConfig(
    upb_StringView type_url, upb_StringView resource_name, upb_Arena* arena,
    envoy_service_status_v3_ClientConfig_GenericXdsConfig* entry) const {
  envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_type_url(entry,
                                                                     type_url);
  envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_name(entry,
                                                                 resource_name);
  envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_client_status(
      entry, client_status_);
  if (!serialized_proto_.empty()) {
    envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_version_info(
        entry, StdStringToUpbString(version_));
    envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_last_updated(
        entry, EncodeTimestamp(update_time_, arena));
    auto* any_field =
        envoy_service_status_v3_ClientConfig_GenericXdsConfig_mutable_xds_config(
            entry, arena);
    google_protobuf_Any_set_type_url(any_field, type_url);
    google_protobuf_Any_set_value(any_field,
                                  StdStringToUpbString(serialized_proto_));
  }
  if (!failed_status_.ok()) {
    auto* update_failure_state = envoy_admin_v3_UpdateFailureState_new(arena);
    envoy_admin_v3_UpdateFailureState_set_details(
        update_failure_state, StdStringToUpbString(failed_status_.message()));
    if (!failed_version_.empty()) {
      envoy_admin_v3_UpdateFailureState_set_version_info(
          update_failure_state, StdStringToUpbString(failed_version_));
      envoy_admin_v3_UpdateFailureState_set_last_update_attempt(
          update_failure_state, EncodeTimestamp(failed_update_time_, arena));
    }
    envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_error_state(
        entry, update_failure_state);
  }
}

//
// XdsClient
//

constexpr absl::string_view XdsClient::kOldStyleAuthority;

XdsClient::XdsClient(
    std::shared_ptr<XdsBootstrap> bootstrap,
    RefCountedPtr<XdsTransportFactory> transport_factory,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine,
    std::unique_ptr<XdsMetricsReporter> metrics_reporter,
    std::string user_agent_name, std::string user_agent_version,
    Duration resource_request_timeout)
    : DualRefCounted<XdsClient>(
          GRPC_TRACE_FLAG_ENABLED(xds_client_refcount) ? "XdsClient" : nullptr),
      bootstrap_(std::move(bootstrap)),
      user_agent_name_(std::move(user_agent_name)),
      user_agent_version_(std::move(user_agent_version)),
      transport_factory_(std::move(transport_factory)),
      request_timeout_(resource_request_timeout),
      xds_federation_enabled_(XdsFederationEnabled()),
      work_serializer_(engine),
      engine_(std::move(engine)),
      metrics_reporter_(std::move(metrics_reporter)) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << this << "] creating xds client";
  CHECK(bootstrap_ != nullptr);
  if (bootstrap_->node() != nullptr) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << this
        << "] xDS node ID: " << bootstrap_->node()->id();
  }
}

XdsClient::~XdsClient() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << this << "] destroying xds client";
}

void XdsClient::Orphaned() {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << this << "] shutting down xds client";
  MutexLock lock(&mu_);
  shutting_down_ = true;
  // Clear cache and any remaining watchers that may not have been cancelled.
  // Note: We move authority_state_map_ out of the way before clearing
  // it, because clearing the map will trigger calls to
  // MaybeRemoveUnsubscribedCacheEntriesForTypeLocked(), which would try to
  // modify the map while we are iterating over it.
  auto authority_state_map = std::move(authority_state_map_);
  authority_state_map.clear();
  invalid_watchers_.clear();
}

RefCountedPtr<XdsClient::XdsChannel> XdsClient::GetOrCreateXdsChannelLocked(
    const XdsBootstrap::XdsServer& server, const char* reason) {
  std::string key = server.Key();
  auto it = xds_channel_map_.find(key);
  if (it != xds_channel_map_.end()) {
    return it->second->Ref(DEBUG_LOCATION, reason);
  }
  // Channel not found, so create a new one.
  auto xds_channel =
      MakeRefCounted<XdsChannel>(WeakRef(DEBUG_LOCATION, "XdsChannel"), server);
  xds_channel_map_[std::move(key)] = xds_channel.get();
  return xds_channel;
}

bool XdsClient::HasUncachedResources(const AuthorityState& authority_state) {
  for (const auto& [_, resource_map] : authority_state.type_map) {
    for (const auto& [_, resource_state] : resource_map) {
      if (resource_state.HasWatchers() &&
          resource_state.client_status() ==
              ResourceState::ClientResourceStatus::REQUESTED) {
        return true;
      }
    }
  }
  return false;
}

void XdsClient::WatchResource(const XdsResourceType* type,
                              absl::string_view name,
                              RefCountedPtr<ResourceWatcherInterface> watcher) {
  // Lambda for handling failure cases.
  auto fail = [&](absl::Status status) mutable {
    {
      MutexLock lock(&mu_);
      MaybeRegisterResourceTypeLocked(type);
      invalid_watchers_.insert(watcher);
    }
    NotifyWatchersOnResourceChanged(std::move(status), {watcher},
                                    ReadDelayHandle::NoWait());
  };
  auto resource_name = ParseXdsResourceName(name, type);
  if (!resource_name.ok()) {
    fail(absl::InvalidArgumentError(
        absl::StrCat("Unable to parse resource name ", name)));
    return;
  }
  // Find server to use.
  std::vector<const XdsBootstrap::XdsServer*> xds_servers;
  if (resource_name->authority != kOldStyleAuthority) {
    auto* authority =
        bootstrap_->LookupAuthority(std::string(resource_name->authority));
    if (authority == nullptr) {
      fail(absl::FailedPreconditionError(
          absl::StrCat("authority \"", resource_name->authority,
                       "\" not present in bootstrap config")));
      return;
    }
    xds_servers = authority->servers();
  }
  if (xds_servers.empty()) xds_servers = bootstrap_->servers();
  MutexLock lock(&mu_);
  MaybeRegisterResourceTypeLocked(type);
  AuthorityState& authority_state =
      authority_state_map_[resource_name->authority];
  auto [it, created_entry] = authority_state.type_map[type].emplace(
      resource_name->key, ResourceState());
  ResourceState& resource_state = it->second;
  resource_state.AddWatcher(watcher);
  if (created_entry) {
    // We try to add new channels in 2 cases:
    // - This is the first resource for this authority (i.e., the list
    //   of channels is empty).
    // - The last channel in the list is failing.  That failure may not
    //   have previously triggered fallback if there were no uncached
    //   resources, but we've just added a new uncached resource,
    //   so we need to trigger fallback now.
    //
    // Note that when we add a channel, it might already be failing
    // due to being used in a different authority.  So we keep going
    // until either we add one that isn't failing or we've added them all.
    if (authority_state.xds_channels.empty() ||
        !authority_state.xds_channels.back()->status().ok()) {
      for (size_t i = authority_state.xds_channels.size();
           i < xds_servers.size(); ++i) {
        authority_state.xds_channels.emplace_back(
            GetOrCreateXdsChannelLocked(*xds_servers[i], "start watch"));
        if (authority_state.xds_channels.back()->status().ok()) {
          break;
        }
      }
    }
  } else {
    // If we already have a cached value for the resource, notify the new
    // watcher immediately.
    if (resource_state.HasResource()) {
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << this << "] returning cached data for " << name;
      NotifyWatchersOnResourceChanged(resource_state.resource(), {watcher},
                                      ReadDelayHandle::NoWait());
    }
    if (!resource_state.failed_status().ok()) {
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << this << "] returning cached error for " << name
          << ": " << resource_state.failed_status();
      NotifyWatchersOnError(resource_state, ReadDelayHandle::NoWait(),
                            {watcher});
    }
  }
  // Make sure all channels are subscribing to the resource.
  for (const auto& channel : authority_state.xds_channels) {
    channel->SubscribeLocked(type, *resource_name);
  }
  // If the channel is not connected, report an error to the watcher.
  absl::Status channel_status = authority_state.xds_channels.back()->status();
  if (!channel_status.ok()) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << this << "] returning cached channel error for "
        << name << ": " << channel_status;
    NotifyWatchersOnError(resource_state, ReadDelayHandle::NoWait(), {watcher},
                          std::move(channel_status));
  }
}

void XdsClient::CancelResourceWatch(const XdsResourceType* type,
                                    absl::string_view name,
                                    ResourceWatcherInterface* watcher,
                                    bool delay_unsubscription) {
  auto resource_name = ParseXdsResourceName(name, type);
  MutexLock lock(&mu_);
  // We cannot be sure whether the watcher is in invalid_watchers_ or in
  // authority_state_map_, so we check both, just to be safe.
  invalid_watchers_.erase(watcher);
  // Find authority.
  if (!resource_name.ok()) return;
  auto authority_it = authority_state_map_.find(resource_name->authority);
  if (authority_it == authority_state_map_.end()) return;
  AuthorityState& authority_state = authority_it->second;
  // Find type map.
  auto type_it = authority_state.type_map.find(type);
  if (type_it == authority_state.type_map.end()) return;
  auto& resource_map = type_it->second;
  // Find resource key.
  auto resource_it = resource_map.find(resource_name->key);
  if (resource_it == resource_map.end()) return;
  ResourceState& resource_state = resource_it->second;
  // Remove watcher.
  resource_state.RemoveWatcher(watcher);
  // If this was the last watcher, clean up.
  if (!resource_state.HasWatchers()) {
    // Unsubscribe from this resource on all XdsChannels.
    for (const auto& xds_channel : authority_state.xds_channels) {
      xds_channel->UnsubscribeLocked(type, *resource_name,
                                     delay_unsubscription);
    }
    // Note: We wait to remove the cache entry until we actualle send
    // the unsubscription message on the ADS stream, so that if a watch is
    // stopped and then started again before we send the next request
    // for that resource type, we don't lose the cache entry without the
    // xDS server knowing it needs to re-send it.
    //
    // Note: Because the cache cleanup may have been triggered by the
    // unsubscription, it's no longer safe to access any of the
    // iterators that we have from above.
  }
}

void XdsClient::MaybeRemoveUnsubscribedCacheEntriesForTypeLocked(
    XdsChannel* xds_channel, const XdsResourceType* type) {
  for (auto authority_it = authority_state_map_.begin();
       authority_it != authority_state_map_.end();) {
    AuthorityState& authority_state = authority_it->second;
    if (authority_state.xds_channels.back() == xds_channel) {
      // Find type map.
      auto type_it = authority_state.type_map.find(type);
      if (type_it != authority_state.type_map.end()) {
        auto& resource_map = type_it->second;
        // Remove the cache entry for any resource without watchers.
        for (auto resource_it = resource_map.begin();
             resource_it != resource_map.end();) {
          ResourceState& resource_state = resource_it->second;
          if (!resource_state.HasWatchers()) {
            resource_map.erase(resource_it++);
          } else {
            ++resource_it;
          }
        }
        // Clean up empty entries in the map.
        if (resource_map.empty()) authority_state.type_map.erase(type_it);
      }
    }
    if (authority_state.type_map.empty()) {
      authority_state_map_.erase(authority_it++);
    } else {
      ++authority_it;
    }
  }
}

void XdsClient::MaybeRegisterResourceTypeLocked(
    const XdsResourceType* resource_type) {
  auto it = resource_types_.find(resource_type->type_url());
  if (it != resource_types_.end()) {
    CHECK(it->second == resource_type);
    return;
  }
  resource_types_.emplace(resource_type->type_url(), resource_type);
  resource_type->InitUpbSymtab(this, def_pool_.ptr());
}

const XdsResourceType* XdsClient::GetResourceTypeLocked(
    absl::string_view resource_type) {
  auto it = resource_types_.find(resource_type);
  if (it != resource_types_.end()) return it->second;
  return nullptr;
}

absl::StatusOr<XdsClient::XdsResourceName> XdsClient::ParseXdsResourceName(
    absl::string_view name, const XdsResourceType* type) {
  // Old-style names use the empty string for authority.
  // authority is set to kOldStyleAuthority to indicate that it's an
  // old-style name.
  if (!xds_federation_enabled_ || !absl::StartsWith(name, "xdstp:")) {
    return XdsResourceName{std::string(kOldStyleAuthority),
                           {std::string(name), {}}};
  }
  // New style name.  Parse URI.
  auto uri = URI::Parse(name);
  if (!uri.ok()) return uri.status();
  // Split the resource type off of the path to get the id.
  std::pair<absl::string_view, absl::string_view> path_parts = absl::StrSplit(
      absl::StripPrefix(uri->path(), "/"), absl::MaxSplits('/', 1));
  if (type->type_url() != path_parts.first) {
    return absl::InvalidArgumentError(
        "xdstp URI path must indicate valid xDS resource type");
  }
  // Canonicalize order of query params.
  std::vector<URI::QueryParam> query_params;
  for (const auto& [key, value] : uri->query_parameter_map()) {
    query_params.emplace_back(
        URI::QueryParam{std::string(key), std::string(value)});
  }
  return XdsResourceName{
      uri->authority(),
      {std::string(path_parts.second), std::move(query_params)}};
}

std::string XdsClient::ConstructFullXdsResourceName(
    absl::string_view authority, absl::string_view resource_type,
    const XdsResourceKey& key) {
  if (authority != kOldStyleAuthority) {
    auto uri = URI::Create("xdstp", std::string(authority),
                           absl::StrCat("/", resource_type, "/", key.id),
                           key.query_params, /*fragment=*/"");
    CHECK(uri.ok());
    return uri->ToString();
  }
  // Old-style name.
  return key.id;
}

void XdsClient::ResetBackoff() {
  MutexLock lock(&mu_);
  for (auto& [_, xds_channel] : xds_channel_map_) {
    xds_channel->ResetBackoff();
  }
}

absl::Status XdsClient::AppendNodeToStatus(const absl::Status& status) const {
  const auto* node = bootstrap_->node();
  if (node == nullptr) return status;
  return absl::Status(
      status.code(), absl::StrCat(status.message(),
                                  " (node ID:", bootstrap_->node()->id(), ")"));
}

void XdsClient::NotifyWatchersOnResourceChanged(
    absl::StatusOr<std::shared_ptr<const XdsResourceType::ResourceData>>
        resource,
    WatcherSet watchers, RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  if (!resource.ok()) resource = AppendNodeToStatus(resource.status());
  work_serializer_.Run(
      [watchers = std::move(watchers), resource = std::move(resource),
       read_delay_handle = std::move(read_delay_handle)]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
            for (const auto& watcher : watchers) {
              watcher->OnGenericResourceChanged(resource, read_delay_handle);
            }
          },
      DEBUG_LOCATION);
}

void XdsClient::NotifyWatchersOnAmbientError(
    absl::Status status, WatcherSet watchers,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  if (!status.ok()) status = AppendNodeToStatus(status);
  work_serializer_.Run(
      [watchers = std::move(watchers), status = std::move(status),
       read_delay_handle = std::move(read_delay_handle)]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
            for (const auto& watcher : watchers) {
              watcher->OnAmbientError(status, read_delay_handle);
            }
          },
      DEBUG_LOCATION);
}

void XdsClient::NotifyWatchersOnError(
    const ResourceState& resource_state,
    RefCountedPtr<ReadDelayHandle> read_delay_handle, WatcherSet watchers,
    absl::Status status) {
  if (watchers.empty()) watchers = resource_state.watchers();
  if (status.ok()) status = resource_state.failed_status();
  if (!resource_state.HasResource()) {
    NotifyWatchersOnResourceChanged(std::move(status), std::move(watchers),
                                    std::move(read_delay_handle));
  } else {
    NotifyWatchersOnAmbientError(std::move(status), std::move(watchers),
                                 std::move(read_delay_handle));
  }
}

void XdsClient::DumpClientConfig(
    std::set<std::string>* string_pool, upb_Arena* arena,
    envoy_service_status_v3_ClientConfig* client_config) {
  // Assemble config dump messages
  // Fill-in the node information
  auto* node =
      envoy_service_status_v3_ClientConfig_mutable_node(client_config, arena);
  PopulateXdsNode(bootstrap_->node(), user_agent_name_, user_agent_version_,
                  node, arena);
  // Dump each resource.
  for (const auto& [authority, authority_state] : authority_state_map_) {
    for (const auto& [type, resource_map] : authority_state.type_map) {
      auto it =
          string_pool
              ->emplace(absl::StrCat("type.googleapis.com/", type->type_url()))
              .first;
      upb_StringView type_url = StdStringToUpbString(*it);
      for (const auto& [resource_key, resource_state] : resource_map) {
        if (!resource_state.HasWatchers()) continue;
        auto it2 = string_pool
                       ->emplace(ConstructFullXdsResourceName(
                           authority, type->type_url(), resource_key))
                       .first;
        upb_StringView resource_name = StdStringToUpbString(*it2);
        envoy_service_status_v3_ClientConfig_GenericXdsConfig* entry =
            envoy_service_status_v3_ClientConfig_add_generic_xds_configs(
                client_config, arena);
        resource_state.FillGenericXdsConfig(type_url, resource_name, arena,
                                            entry);
      }
    }
  }
}

void XdsClient::ReportResourceCounts(
    absl::FunctionRef<void(const ResourceCountLabels&, uint64_t)> func) {
  ResourceCountLabels labels;
  for (const auto& [authority, authority_state] : authority_state_map_) {
    labels.xds_authority = authority;
    for (const auto& [type, resource_map] : authority_state.type_map) {
      labels.resource_type = type->type_url();
      // Count the number of entries in each state.
      std::map<absl::string_view, uint64_t> counts;
      for (const auto& [_, resource_state] : resource_map) {
        if (resource_state.HasWatchers()) {
          ++counts[resource_state.CacheStateString()];
        }
      }
      // Report the count for each state.
      for (const auto& [state, count] : counts) {
        labels.cache_state = state;
        func(labels, count);
      }
    }
  }
}

void XdsClient::ReportServerConnections(
    absl::FunctionRef<void(absl::string_view, bool)> func) {
  for (const auto& [_, xds_channel] : xds_channel_map_) {
    func(xds_channel->server_uri(), xds_channel->status().ok());
  }
}

}  // namespace grpc_core
