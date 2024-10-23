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
#include "absl/types/optional.h"
#include "envoy/config/core/v3/base.upb.h"
#include "envoy/service/status/v3/csds.upb.h"
#include "google/protobuf/any.upb.h"
#include "google/protobuf/timestamp.upb.h"
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
  absl::optional<EventEngine::TaskHandle> timer_handle_
      ABSL_GUARDED_BY(&XdsClient::mu_);

  bool shutting_down_ = false;
};

// Contains an ADS call to the xds server.
class XdsClient::XdsChannel::AdsCall final
    : public InternallyRefCounted<AdsCall> {
 public:
  // The ctor and dtor should not be used directly.
  explicit AdsCall(RefCountedPtr<RetryableCall<AdsCall>> retryable_call);

  void Orphan() override;

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

  class AdsResponseParser final : public XdsApi::AdsResponseParserInterface {
   public:
    struct Result {
      const XdsResourceType* type;
      std::string type_url;
      std::string version;
      std::string nonce;
      std::vector<std::string> errors;
      std::map<std::string /*authority*/, std::set<XdsResourceKey>>
          resources_seen;
      uint64_t num_valid_resources = 0;
      uint64_t num_invalid_resources = 0;
      RefCountedPtr<ReadDelayHandle> read_delay_handle;
    };

    explicit AdsResponseParser(AdsCall* ads_call) : ads_call_(ads_call) {}

    absl::Status ProcessAdsResponseFields(AdsResponseFields fields) override
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

    void ParseResource(upb_Arena* arena, size_t idx, absl::string_view type_url,
                       absl::string_view resource_name,
                       absl::string_view serialized_resource) override
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

    void ResourceWrapperParsingFailed(size_t idx,
                                      absl::string_view message) override;

    Result TakeResult() { return std::move(result_); }

   private:
    XdsClient* xds_client() const { return ads_call_->xds_client(); }

    AdsCall* ads_call_;
    const Timestamp update_time_ = Timestamp::Now();
    Result result_;
  };

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
      ResourceState& state = authority_state.resource_map[type_][name_.key];
      if (state.resource != nullptr) return;
      // Start timer.
      ads_call_ = std::move(ads_call);
      timer_handle_ = ads_call_->xds_client()->engine()->RunAfter(
          ads_call_->xds_client()->request_timeout_,
          [self = Ref(DEBUG_LOCATION, "timer")]() {
            ApplicationCallbackExecCtx callback_exec_ctx;
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
        ResourceState& state = authority_state.resource_map[type_][name_.key];
        // We might have received the resource after the timer fired but before
        // the callback ran.
        if (state.resource == nullptr) {
          GRPC_TRACE_LOG(xds_client, INFO)
              << "[xds_client " << ads_call_->xds_client() << "] xds server "
              << ads_call_->xds_channel()->server_.server_uri()
              << ": timeout obtaining resource {type=" << type_->type_url()
              << " name="
              << XdsClient::ConstructFullXdsResourceName(
                     name_.authority, type_->type_url(), name_.key)
              << "} from xds server";
          resource_seen_ = true;
          state.meta.client_status = XdsApi::ResourceMetadata::DOES_NOT_EXIST;
          ads_call_->xds_client()->NotifyWatchersOnResourceDoesNotExist(
              state.watchers, ReadDelayHandle::NoWait());
        }
      }
      ads_call_->xds_client()->work_serializer_.DrainQueue();
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
    absl::optional<EventEngine::TaskHandle> timer_handle_
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

  void SendMessageLocked(const XdsResourceType* type)
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
      << " for server " << server.server_uri();
  absl::Status status;
  transport_ = xds_client_->transport_factory_->GetTransport(server, &status);
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
      << " for server " << server_.server_uri();
  xds_client_.reset(DEBUG_LOCATION, "XdsChannel");
}

// This method should only ever be called when holding the lock, but we can't
// use a ABSL_EXCLUSIVE_LOCKS_REQUIRED annotation, because Orphan() will be
// called from DualRefCounted::Unref, which cannot have a lock annotation for
// a lock in this subclass.
void XdsClient::XdsChannel::Orphaned() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] orphaning xds channel " << this
      << " for server " << server_.server_uri();
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

XdsClient::XdsChannel::AdsCall* XdsClient::XdsChannel::ads_call() const {
  return ads_call_->call();
}

void XdsClient::XdsChannel::SubscribeLocked(const XdsResourceType* type,
                                            const XdsResourceName& name) {
  if (ads_call_ == nullptr) {
    // Start the ADS call if this is the first request.
    ads_call_.reset(
        new RetryableCall<AdsCall>(WeakRef(DEBUG_LOCATION, "XdsChannel+ads")));
    // Note: AdsCall's ctor will automatically subscribe to all
    // resources that the XdsClient already has watchers for, so we can
    // return here.
    return;
  }
  // If the ADS call is in backoff state, we don't need to do anything now
  // because when the call is restarted it will resend all necessary requests.
  if (ads_call() == nullptr) return;
  // Subscribe to this resource if the ADS call is active.
  ads_call()->SubscribeLocked(type, name, /*delay_send=*/false);
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
    for (const auto& type_resource : authority_state.resource_map) {
      for (const auto& key_state : type_resource.second) {
        authority_state.xds_channels.back()->SubscribeLocked(
            type_resource.first, {authority, key_state.first});
      }
    }
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << xds_client_.get() << "] authority " << authority
        << ": added fallback server " << xds_servers[i]->server_uri() << " ("
        << authority_state.xds_channels.back()->status().ToString() << ")";
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
  for (auto& authority : xds_client_->authority_state_map_) {
    auto& channels = authority.second.xds_channels;
    // Skip if channel is active.
    if (channels.back() == this) continue;
    auto channel_it = std::find(channels.begin(), channels.end(), this);
    // Skip if this is not on the list
    if (channel_it != channels.end()) {
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << xds_client_.get() << "] authority "
          << authority.first << ": Falling forward to " << server_.server_uri();
      // Lower priority channels are no longer needed, connection is back!
      channels.erase(channel_it + 1, channels.end());
    }
  }
}

void XdsClient::XdsChannel::OnConnectivityFailure(absl::Status status) {
  {
    MutexLock lock(&xds_client_->mu_);
    SetChannelStatusLocked(std::move(status));
  }
  xds_client_->work_serializer_.DrainQueue();
}

void XdsClient::XdsChannel::SetChannelStatusLocked(absl::Status status) {
  if (shutting_down_) return;
  status = absl::Status(status.code(), absl::StrCat("xDS channel for server ",
                                                    server_.server_uri(), ": ",
                                                    status.message()));
  LOG(INFO) << "[xds_client " << xds_client() << "] " << status;
  // If the node ID is set, append that to the status message that we send to
  // the watchers, so that it will appear in log messages visible to users.
  const auto* node = xds_client_->bootstrap_->node();
  if (node != nullptr) {
    status = absl::Status(
        status.code(),
        absl::StrCat(status.message(),
                     " (node ID:", xds_client_->bootstrap_->node()->id(), ")"));
  }
  // If status was previously OK, report that the channel has gone unhealthy.
  if (status_.ok() && xds_client_->metrics_reporter_ != nullptr) {
    xds_client_->metrics_reporter_->ReportServerFailure(server_.server_uri());
  }
  // Save status in channel, so that we can immediately generate an
  // error for any new watchers that may be started.
  status_ = status;
  // Find all watchers for this channel.
  std::set<RefCountedPtr<ResourceWatcherInterface>> watchers;
  for (auto& a : xds_client_->authority_state_map_) {  // authority
    if (a.second.xds_channels.empty() || a.second.xds_channels.back() != this ||
        MaybeFallbackLocked(a.first, a.second)) {
      continue;
    }
    for (const auto& t : a.second.resource_map) {  // type
      for (const auto& r : t.second) {             // resource id
        for (const auto& w : r.second.watchers) {  // watchers
          watchers.insert(w.second);
        }
      }
    }
  }
  if (!watchers.empty()) {
    // Enqueue notification for the watchers.
    xds_client_->work_serializer_.Schedule(
        [watchers = std::move(watchers), status = std::move(status)]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(xds_client_->work_serializer_) {
              for (const auto& watcher : watchers) {
                watcher->OnError(status, ReadDelayHandle::NoWait());
              }
            },
        DEBUG_LOCATION);
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
      << xds_channel()->server_.server_uri()
      << ": start new call from retryable call " << this;
  call_ = MakeOrphanable<T>(
      this->Ref(DEBUG_LOCATION, "RetryableCall+start_new_call"));
}

template <typename T>
void XdsClient::XdsChannel::RetryableCall<T>::StartRetryTimerLocked() {
  if (shutting_down_) return;
  const Duration delay = backoff_.NextAttemptDelay();
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_channel()->xds_client() << "] xds server "
      << xds_channel()->server_.server_uri()
      << ": call attempt failed; retry timer will fire in " << delay.millis()
      << "ms.";
  timer_handle_ = xds_channel()->xds_client()->engine()->RunAfter(
      delay,
      [self = this->Ref(DEBUG_LOCATION, "RetryableCall+retry_timer_start")]() {
        ApplicationCallbackExecCtx callback_exec_ctx;
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
        << xds_channel()->server_.server_uri()
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
// XdsClient::XdsChannel::AdsCall::AdsResponseParser
//

absl::Status
XdsClient::XdsChannel::AdsCall::AdsResponseParser::ProcessAdsResponseFields(
    AdsResponseFields fields) {
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << ads_call_->xds_client() << "] xds server "
      << ads_call_->xds_channel()->server_.server_uri()
      << ": received ADS response: type_url=" << fields.type_url
      << ", version=" << fields.version << ", nonce=" << fields.nonce
      << ", num_resources=" << fields.num_resources;
  result_.type =
      ads_call_->xds_client()->GetResourceTypeLocked(fields.type_url);
  if (result_.type == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown resource type ", fields.type_url));
  }
  result_.type_url = std::move(fields.type_url);
  result_.version = std::move(fields.version);
  result_.nonce = std::move(fields.nonce);
  result_.read_delay_handle =
      MakeRefCounted<AdsReadDelayHandle>(ads_call_->Ref());
  return absl::OkStatus();
}

namespace {

// Build a resource metadata struct for ADS result accepting methods and CSDS.
XdsApi::ResourceMetadata CreateResourceMetadataAcked(
    std::string serialized_proto, std::string version, Timestamp update_time) {
  XdsApi::ResourceMetadata resource_metadata;
  resource_metadata.serialized_proto = std::move(serialized_proto);
  resource_metadata.update_time = update_time;
  resource_metadata.version = std::move(version);
  resource_metadata.client_status = XdsApi::ResourceMetadata::ACKED;
  return resource_metadata;
}

// Update resource_metadata for NACK.
void UpdateResourceMetadataNacked(const std::string& version,
                                  const std::string& details,
                                  Timestamp update_time,
                                  XdsApi::ResourceMetadata* resource_metadata) {
  resource_metadata->client_status = XdsApi::ResourceMetadata::NACKED;
  resource_metadata->failed_version = version;
  resource_metadata->failed_details = details;
  resource_metadata->failed_update_time = update_time;
}

}  // namespace

void XdsClient::XdsChannel::AdsCall::AdsResponseParser::ParseResource(
    upb_Arena* arena, size_t idx, absl::string_view type_url,
    absl::string_view resource_name, absl::string_view serialized_resource) {
  std::string error_prefix = absl::StrCat(
      "resource index ", idx, ": ",
      resource_name.empty() ? "" : absl::StrCat(resource_name, ": "));
  // Check the type_url of the resource.
  if (result_.type_url != type_url) {
    result_.errors.emplace_back(
        absl::StrCat(error_prefix, "incorrect resource type \"", type_url,
                     "\" (should be \"", result_.type_url, "\")"));
    ++result_.num_invalid_resources;
    return;
  }
  // Parse the resource.
  XdsResourceType::DecodeContext context = {
      xds_client(), ads_call_->xds_channel()->server_, &xds_client_trace,
      xds_client()->def_pool_.ptr(), arena};
  XdsResourceType::DecodeResult decode_result =
      result_.type->Decode(context, serialized_resource);
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
      result_.errors.emplace_back(absl::StrCat(
          error_prefix, decode_result.resource.status().ToString()));
      ++result_.num_invalid_resources;
      return;
    }
  }
  // If decoding failed, make sure we include the error in the NACK.
  const absl::Status& decode_status = decode_result.resource.status();
  if (!decode_status.ok()) {
    result_.errors.emplace_back(
        absl::StrCat(error_prefix, decode_status.ToString()));
  }
  // Check the resource name.
  auto parsed_resource_name =
      xds_client()->ParseXdsResourceName(resource_name, result_.type);
  if (!parsed_resource_name.ok()) {
    result_.errors.emplace_back(
        absl::StrCat(error_prefix, "Cannot parse xDS resource name"));
    ++result_.num_invalid_resources;
    return;
  }
  // Cancel resource-does-not-exist timer, if needed.
  auto timer_it = ads_call_->state_map_.find(result_.type);
  if (timer_it != ads_call_->state_map_.end()) {
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
  // Found authority, so look up type.
  AuthorityState& authority_state = authority_it->second;
  auto type_it = authority_state.resource_map.find(result_.type);
  if (type_it == authority_state.resource_map.end()) {
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
  if (result_.type->AllResourcesRequiredInSotW()) {
    result_.resources_seen[parsed_resource_name->authority].insert(
        parsed_resource_name->key);
  }
  // If we previously ignored the resource's deletion, log that we're
  // now re-adding it.
  if (resource_state.ignored_deletion) {
    LOG(INFO) << "[xds_client " << xds_client() << "] xds server "
              << ads_call_->xds_channel()->server_.server_uri()
              << ": server returned new version of resource for which we "
                 "previously ignored a deletion: type "
              << type_url << " name " << resource_name;
    resource_state.ignored_deletion = false;
  }
  // Update resource state based on whether the resource is valid.
  if (!decode_status.ok()) {
    xds_client()->NotifyWatchersOnErrorLocked(
        resource_state.watchers,
        absl::UnavailableError(
            absl::StrCat("invalid resource: ", decode_status.ToString())),
        result_.read_delay_handle);
    UpdateResourceMetadataNacked(result_.version, decode_status.ToString(),
                                 update_time_, &resource_state.meta);
    ++result_.num_invalid_resources;
    return;
  }
  // Resource is valid.
  ++result_.num_valid_resources;
  // If it didn't change, ignore it.
  if (resource_state.resource != nullptr &&
      result_.type->ResourcesEqual(resource_state.resource.get(),
                                   decode_result.resource->get())) {
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << xds_client() << "] " << result_.type_url
        << " resource " << resource_name << " identical to current, ignoring.";
    return;
  }
  // Update the resource state.
  resource_state.resource = std::move(*decode_result.resource);
  resource_state.meta = CreateResourceMetadataAcked(
      std::string(serialized_resource), result_.version, update_time_);
  // Notify watchers.
  auto& watchers_list = resource_state.watchers;
  xds_client()->work_serializer_.Schedule(
      [watchers_list, value = resource_state.resource,
       read_delay_handle = result_.read_delay_handle]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&xds_client()->work_serializer_) {
            for (const auto& p : watchers_list) {
              p.first->OnGenericResourceChanged(value, read_delay_handle);
            }
          },
      DEBUG_LOCATION);
}

void XdsClient::XdsChannel::AdsCall::AdsResponseParser::
    ResourceWrapperParsingFailed(size_t idx, absl::string_view message) {
  result_.errors.emplace_back(
      absl::StrCat("resource index ", idx, ": ", message));
  ++result_.num_invalid_resources;
}

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
      << xds_channel()->server_.server_uri()
      << ": starting ADS call (ads_call: " << this
      << ", streaming_call: " << streaming_call_.get() << ")";
  // If this is a reconnect, add any necessary subscriptions from what's
  // already in the cache.
  for (auto& a : xds_client()->authority_state_map_) {
    const std::string& authority = a.first;
    auto it = std::find(a.second.xds_channels.begin(),
                        a.second.xds_channels.end(), xds_channel());
    // Skip authorities that are not using this xDS channel. The channel can be
    // anywhere in the list.
    if (it == a.second.xds_channels.end()) continue;
    for (const auto& t : a.second.resource_map) {
      const XdsResourceType* type = t.first;
      for (const auto& r : t.second) {
        const XdsResourceKey& resource_key = r.first;
        SubscribeLocked(type, {authority, resource_key}, /*delay_send=*/true);
      }
    }
  }
  // Send initial message if we added any subscriptions above.
  for (const auto& p : state_map_) {
    SendMessageLocked(p.first);
  }
  streaming_call_->StartRecvMessage();
}

void XdsClient::XdsChannel::AdsCall::Orphan() {
  state_map_.clear();
  // Note that the initial ref is held by the StreamEventHandler, which
  // will be destroyed when streaming_call_ is destroyed, which may not happen
  // here, since there may be other refs held to streaming_call_ by internal
  // callbacks.
  streaming_call_.reset();
}

void XdsClient::XdsChannel::AdsCall::SendMessageLocked(
    const XdsResourceType* type)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
  // Buffer message sending if an existing message is in flight.
  if (send_message_pending_ != nullptr) {
    buffered_requests_.insert(type);
    return;
  }
  auto& state = state_map_[type];
  std::string serialized_message = xds_client()->api_.CreateAdsRequest(
      type->type_url(), xds_channel()->resource_type_version_map_[type],
      state.nonce, ResourceNamesForRequest(type), state.status,
      !sent_initial_message_);
  sent_initial_message_ = true;
  GRPC_TRACE_LOG(xds_client, INFO)
      << "[xds_client " << xds_client() << "] xds server "
      << xds_channel()->server_.server_uri()
      << ": sending ADS request: type=" << type->type_url()
      << " version=" << xds_channel()->resource_type_version_map_[type]
      << " nonce=" << state.nonce << " error=" << state.status;
  state.status = absl::OkStatus();
  streaming_call_->SendMessage(std::move(serialized_message));
  send_message_pending_ = type;
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
  }
  // Don't need to send unsubscription message if this was the last
  // resource we were subscribed to, since we'll be closing the stream
  // immediately in that case.
  if (!delay_unsubscription && HasSubscribedResources()) {
    SendMessageLocked(type);
  }
}

bool XdsClient::XdsChannel::AdsCall::HasSubscribedResources() const {
  for (const auto& p : state_map_) {
    if (!p.second.subscribed_resources.empty()) return true;
  }
  return false;
}

void XdsClient::XdsChannel::AdsCall::OnRequestSent(bool ok) {
  MutexLock lock(&xds_client()->mu_);
  // For each resource that was in the message we just sent, start the
  // resource timer if needed.
  if (ok) {
    auto& resource_type_state = state_map_[send_message_pending_];
    for (const auto& p : resource_type_state.subscribed_resources) {
      for (auto& q : p.second) {
        q.second->MaybeMarkSubscriptionSendComplete(
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

void XdsClient::XdsChannel::AdsCall::OnRecvMessage(absl::string_view payload) {
  // Needs to be destroyed after the mutex is released.
  RefCountedPtr<ReadDelayHandle> read_delay_handle;
  {
    MutexLock lock(&xds_client()->mu_);
    if (!IsCurrentCallOnChannel()) return;
    // Parse and validate the response.
    AdsResponseParser parser(this);
    absl::Status status = xds_client()->api_.ParseAdsResponse(payload, &parser);
    // This includes a handle that will trigger an ADS read.
    AdsResponseParser::Result result = parser.TakeResult();
    read_delay_handle = std::move(result.read_delay_handle);
    if (!status.ok()) {
      // Ignore unparsable response.
      LOG(ERROR) << "[xds_client " << xds_client() << "] xds server "
                 << xds_channel()->server_.server_uri()
                 << ": error parsing ADS response (" << status
                 << ") -- ignoring";
    } else {
      seen_response_ = true;
      xds_channel()->SetHealthyLocked();
      // Update nonce.
      auto& state = state_map_[result.type];
      state.nonce = result.nonce;
      // If we got an error, set state.status so that we'll NACK the update.
      if (!result.errors.empty()) {
        state.status = absl::UnavailableError(
            absl::StrCat("xDS response validation errors: [",
                         absl::StrJoin(result.errors, "; "), "]"));
        LOG(ERROR) << "[xds_client " << xds_client() << "] xds server "
                   << xds_channel()->server_.server_uri()
                   << ": ADS response invalid for resource type "
                   << result.type_url << " version " << result.version
                   << ", will NACK: nonce=" << state.nonce
                   << " status=" << state.status;
      }
      // Delete resources not seen in update if needed.
      if (result.type->AllResourcesRequiredInSotW()) {
        for (auto& a : xds_client()->authority_state_map_) {
          const std::string& authority = a.first;
          AuthorityState& authority_state = a.second;
          // Skip authorities that are not using this xDS channel.
          if (authority_state.xds_channels.back() != xds_channel()) {
            continue;
          }
          auto seen_authority_it = result.resources_seen.find(authority);
          // Find this resource type.
          auto type_it = authority_state.resource_map.find(result.type);
          if (type_it == authority_state.resource_map.end()) continue;
          // Iterate over resource ids.
          for (auto& r : type_it->second) {
            const XdsResourceKey& resource_key = r.first;
            ResourceState& resource_state = r.second;
            if (seen_authority_it == result.resources_seen.end() ||
                seen_authority_it->second.find(resource_key) ==
                    seen_authority_it->second.end()) {
              // If the resource was newly requested but has not yet been
              // received, we don't want to generate an error for the
              // watchers, because this ADS response may be in reaction to an
              // earlier request that did not yet request the new resource, so
              // its absence from the response does not necessarily indicate
              // that the resource does not exist.  For that case, we rely on
              // the request timeout instead.
              if (resource_state.resource == nullptr) continue;
              if (xds_channel()->server_.IgnoreResourceDeletion()) {
                if (!resource_state.ignored_deletion) {
                  LOG(ERROR)
                      << "[xds_client " << xds_client() << "] xds server "
                      << xds_channel()->server_.server_uri()
                      << ": ignoring deletion for resource type "
                      << result.type_url << " name "
                      << XdsClient::ConstructFullXdsResourceName(
                             authority, result.type_url.c_str(), resource_key);
                  resource_state.ignored_deletion = true;
                }
              } else {
                resource_state.resource.reset();
                resource_state.meta.client_status =
                    XdsApi::ResourceMetadata::DOES_NOT_EXIST;
                xds_client()->NotifyWatchersOnResourceDoesNotExist(
                    resource_state.watchers, read_delay_handle);
              }
            }
          }
        }
      }
      // If we had valid resources or the update was empty, update the version.
      if (result.num_valid_resources > 0 || result.errors.empty()) {
        xds_channel()->resource_type_version_map_[result.type] =
            std::move(result.version);
      }
      // Send ACK or NACK.
      SendMessageLocked(result.type);
    }
    // Update metrics.
    if (xds_client()->metrics_reporter_ != nullptr) {
      xds_client()->metrics_reporter_->ReportResourceUpdates(
          xds_channel()->server_.server_uri(), result.type_url,
          result.num_valid_resources, result.num_invalid_resources);
    }
  }
  xds_client()->work_serializer_.DrainQueue();
}

void XdsClient::XdsChannel::AdsCall::OnStatusReceived(absl::Status status) {
  {
    MutexLock lock(&xds_client()->mu_);
    GRPC_TRACE_LOG(xds_client, INFO)
        << "[xds_client " << xds_client() << "] xds server "
        << xds_channel()->server_.server_uri()
        << ": ADS call status received (xds_channel=" << xds_channel()
        << ", ads_call=" << this << ", streaming_call=" << streaming_call_.get()
        << "): " << status;
    // Cancel any does-not-exist timers that may be pending.
    for (const auto& p : state_map_) {
      for (const auto& q : p.second.subscribed_resources) {
        for (auto& r : q.second) {
          r.second->MaybeCancelTimer();
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
  xds_client()->work_serializer_.DrainQueue();
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
    for (auto& a : it->second.subscribed_resources) {
      const std::string& authority = a.first;
      for (auto& p : a.second) {
        const XdsResourceKey& resource_key = p.first;
        resource_names.emplace_back(XdsClient::ConstructFullXdsResourceName(
            authority, type->type_url(), resource_key));
        OrphanablePtr<ResourceTimer>& resource_timer = p.second;
        resource_timer->MarkSubscriptionSendStarted();
      }
    }
  }
  return resource_names;
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
      transport_factory_(std::move(transport_factory)),
      request_timeout_(resource_request_timeout),
      xds_federation_enabled_(XdsFederationEnabled()),
      api_(this, &xds_client_trace, bootstrap_->node(), &def_pool_,
           std::move(user_agent_name), std::move(user_agent_version)),
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
  authority_state_map_.clear();
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
  for (const auto& type_resource : authority_state.resource_map) {
    for (const auto& key_state : type_resource.second) {
      if (key_state.second.meta.client_status ==
          XdsApi::ResourceMetadata::REQUESTED) {
        return true;
      }
    }
  }
  return false;
}

void XdsClient::WatchResource(const XdsResourceType* type,
                              absl::string_view name,
                              RefCountedPtr<ResourceWatcherInterface> watcher) {
  ResourceWatcherInterface* w = watcher.get();
  // Lambda for handling failure cases.
  auto fail = [&](absl::Status status) mutable {
    {
      MutexLock lock(&mu_);
      MaybeRegisterResourceTypeLocked(type);
      invalid_watchers_[w] = watcher;
    }
    work_serializer_.Run(
        [watcher = std::move(watcher), status = std::move(status)]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
              watcher->OnError(status, ReadDelayHandle::NoWait());
            },
        DEBUG_LOCATION);
  };
  auto resource_name = ParseXdsResourceName(name, type);
  if (!resource_name.ok()) {
    fail(absl::UnavailableError(
        absl::StrCat("Unable to parse resource name ", name)));
    return;
  }
  // Find server to use.
  std::vector<const XdsBootstrap::XdsServer*> xds_servers;
  if (resource_name->authority != kOldStyleAuthority) {
    auto* authority =
        bootstrap_->LookupAuthority(std::string(resource_name->authority));
    if (authority == nullptr) {
      fail(absl::UnavailableError(
          absl::StrCat("authority \"", resource_name->authority,
                       "\" not present in bootstrap config")));
      return;
    }
    xds_servers = authority->servers();
  }
  if (xds_servers.empty()) xds_servers = bootstrap_->servers();
  {
    MutexLock lock(&mu_);
    MaybeRegisterResourceTypeLocked(type);

    AuthorityState& authority_state =
        authority_state_map_[resource_name->authority];
    auto it_is_new = authority_state.resource_map[type].emplace(
        resource_name->key, ResourceState());
    bool first_watcher_for_resource = it_is_new.second;
    ResourceState& resource_state = it_is_new.first->second;
    resource_state.watchers[w] = watcher;
    if (first_watcher_for_resource) {
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
      for (const auto& channel : authority_state.xds_channels) {
        channel->SubscribeLocked(type, *resource_name);
      }
    } else {
      // If we already have a cached value for the resource, notify the new
      // watcher immediately.
      if (resource_state.resource != nullptr) {
        GRPC_TRACE_LOG(xds_client, INFO)
            << "[xds_client " << this << "] returning cached listener data for "
            << name;
        work_serializer_.Schedule(
            [watcher, value = resource_state.resource]()
                ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
                  watcher->OnGenericResourceChanged(value,
                                                    ReadDelayHandle::NoWait());
                },
            DEBUG_LOCATION);
      } else if (resource_state.meta.client_status ==
                 XdsApi::ResourceMetadata::DOES_NOT_EXIST) {
        GRPC_TRACE_LOG(xds_client, INFO)
            << "[xds_client " << this
            << "] reporting cached does-not-exist for " << name;
        work_serializer_.Schedule(
            [watcher]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
              watcher->OnResourceDoesNotExist(ReadDelayHandle::NoWait());
            },
            DEBUG_LOCATION);
      } else if (resource_state.meta.client_status ==
                 XdsApi::ResourceMetadata::NACKED) {
        GRPC_TRACE_LOG(xds_client, INFO)
            << "[xds_client " << this
            << "] reporting cached validation failure for " << name << ": "
            << resource_state.meta.failed_details;
        std::string details = resource_state.meta.failed_details;
        const auto* node = bootstrap_->node();
        if (node != nullptr) {
          absl::StrAppend(&details, " (node ID:", bootstrap_->node()->id(),
                          ")");
        }
        work_serializer_.Schedule(
            [watcher, details = std::move(details)]()
                ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
                  watcher->OnError(absl::UnavailableError(absl::StrCat(
                                       "invalid resource: ", details)),
                                   ReadDelayHandle::NoWait());
                },
            DEBUG_LOCATION);
      }
    }
    absl::Status channel_status = authority_state.xds_channels.back()->status();
    if (!channel_status.ok()) {
      GRPC_TRACE_LOG(xds_client, INFO)
          << "[xds_client " << this << "] returning cached channel error for "
          << name << ": " << channel_status;
      work_serializer_.Schedule(
          [watcher = std::move(watcher), status = std::move(channel_status)]()
              ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) mutable {
                watcher->OnError(std::move(status), ReadDelayHandle::NoWait());
              },
          DEBUG_LOCATION);
    }
  }
  work_serializer_.DrainQueue();
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
  auto type_it = authority_state.resource_map.find(type);
  if (type_it == authority_state.resource_map.end()) return;
  auto& type_map = type_it->second;
  // Find resource key.
  auto resource_it = type_map.find(resource_name->key);
  if (resource_it == type_map.end()) return;
  ResourceState& resource_state = resource_it->second;
  // Remove watcher.
  resource_state.watchers.erase(watcher);
  // Clean up empty map entries, if any.
  if (resource_state.watchers.empty()) {
    if (resource_state.ignored_deletion) {
      LOG(INFO) << "[xds_client " << this
                << "] unsubscribing from a resource for which we "
                << "previously ignored a deletion: type " << type->type_url()
                << " name " << name;
    }
    for (const auto& xds_channel : authority_state.xds_channels) {
      xds_channel->UnsubscribeLocked(type, *resource_name,
                                     delay_unsubscription);
    }
    type_map.erase(resource_it);
    if (type_map.empty()) {
      authority_state.resource_map.erase(type_it);
      if (authority_state.resource_map.empty()) {
        authority_state.xds_channels.clear();
      }
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
  for (const auto& p : uri->query_parameter_map()) {
    query_params.emplace_back(
        URI::QueryParam{std::string(p.first), std::string(p.second)});
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
  for (auto& p : xds_channel_map_) {
    p.second->ResetBackoff();
  }
}

void XdsClient::NotifyWatchersOnErrorLocked(
    const std::map<ResourceWatcherInterface*,
                   RefCountedPtr<ResourceWatcherInterface>>& watchers,
    absl::Status status, RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  const auto* node = bootstrap_->node();
  if (node != nullptr) {
    status = absl::Status(
        status.code(),
        absl::StrCat(status.message(), " (node ID:", node->id(), ")"));
  }
  work_serializer_.Schedule(
      [watchers, status = std::move(status),
       read_delay_handle = std::move(read_delay_handle)]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
            for (const auto& p : watchers) {
              p.first->OnError(status, read_delay_handle);
            }
          },
      DEBUG_LOCATION);
}

void XdsClient::NotifyWatchersOnResourceDoesNotExist(
    const std::map<ResourceWatcherInterface*,
                   RefCountedPtr<ResourceWatcherInterface>>& watchers,
    RefCountedPtr<ReadDelayHandle> read_delay_handle) {
  work_serializer_.Schedule(
      [watchers, read_delay_handle = std::move(read_delay_handle)]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
            for (const auto& p : watchers) {
              p.first->OnResourceDoesNotExist(read_delay_handle);
            }
          },
      DEBUG_LOCATION);
}

namespace {

google_protobuf_Timestamp* EncodeTimestamp(Timestamp value, upb_Arena* arena) {
  google_protobuf_Timestamp* timestamp = google_protobuf_Timestamp_new(arena);
  gpr_timespec timespec = value.as_timespec(GPR_CLOCK_REALTIME);
  google_protobuf_Timestamp_set_seconds(timestamp, timespec.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, timespec.tv_nsec);
  return timestamp;
}

void FillGenericXdsConfig(
    const XdsApi::ResourceMetadata& metadata, upb_StringView type_url,
    upb_StringView resource_name, upb_Arena* arena,
    envoy_service_status_v3_ClientConfig_GenericXdsConfig* entry) {
  envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_type_url(entry,
                                                                     type_url);
  envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_name(entry,
                                                                 resource_name);
  envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_client_status(
      entry, metadata.client_status);
  if (!metadata.serialized_proto.empty()) {
    envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_version_info(
        entry, StdStringToUpbString(metadata.version));
    envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_last_updated(
        entry, EncodeTimestamp(metadata.update_time, arena));
    auto* any_field =
        envoy_service_status_v3_ClientConfig_GenericXdsConfig_mutable_xds_config(
            entry, arena);
    google_protobuf_Any_set_type_url(any_field, type_url);
    google_protobuf_Any_set_value(
        any_field, StdStringToUpbString(metadata.serialized_proto));
  }
  if (metadata.client_status == XdsApi::ResourceMetadata::NACKED) {
    auto* update_failure_state = envoy_admin_v3_UpdateFailureState_new(arena);
    envoy_admin_v3_UpdateFailureState_set_details(
        update_failure_state, StdStringToUpbString(metadata.failed_details));
    envoy_admin_v3_UpdateFailureState_set_version_info(
        update_failure_state, StdStringToUpbString(metadata.failed_version));
    envoy_admin_v3_UpdateFailureState_set_last_update_attempt(
        update_failure_state,
        EncodeTimestamp(metadata.failed_update_time, arena));
    envoy_service_status_v3_ClientConfig_GenericXdsConfig_set_error_state(
        entry, update_failure_state);
  }
}

}  // namespace

void XdsClient::DumpClientConfig(
    std::set<std::string>* string_pool, upb_Arena* arena,
    envoy_service_status_v3_ClientConfig* client_config) {
  // Assemble config dump messages
  // Fill-in the node information
  auto* node =
      envoy_service_status_v3_ClientConfig_mutable_node(client_config, arena);
  api_.PopulateNode(node, arena);
  // Dump each resource.
  for (const auto& a : authority_state_map_) {  // authority
    const std::string& authority = a.first;
    for (const auto& t : a.second.resource_map) {  // type
      const XdsResourceType* type = t.first;
      auto it =
          string_pool
              ->emplace(absl::StrCat("type.googleapis.com/", type->type_url()))
              .first;
      upb_StringView type_url = StdStringToUpbString(*it);
      for (const auto& r : t.second) {  // resource id
        auto it2 = string_pool
                       ->emplace(ConstructFullXdsResourceName(
                           authority, type->type_url(), r.first))
                       .first;
        upb_StringView resource_name = StdStringToUpbString(*it2);
        envoy_service_status_v3_ClientConfig_GenericXdsConfig* entry =
            envoy_service_status_v3_ClientConfig_add_generic_xds_configs(
                client_config, arena);
        FillGenericXdsConfig(r.second.meta, type_url, resource_name, arena,
                             entry);
      }
    }
  }
}

namespace {

absl::string_view CacheStateForEntry(const XdsApi::ResourceMetadata& metadata,
                                     bool resource_cached) {
  switch (metadata.client_status) {
    case XdsApi::ResourceMetadata::REQUESTED:
      return "requested";
    case XdsApi::ResourceMetadata::DOES_NOT_EXIST:
      return "does_not_exist";
    case XdsApi::ResourceMetadata::ACKED:
      return "acked";
    case XdsApi::ResourceMetadata::NACKED:
      return resource_cached ? "nacked_but_cached" : "nacked";
  }
  Crash("unknown resource state");
}

}  // namespace

void XdsClient::ReportResourceCounts(
    absl::FunctionRef<void(const ResourceCountLabels&, uint64_t)> func) {
  ResourceCountLabels labels;
  for (const auto& a : authority_state_map_) {  // authority
    labels.xds_authority = a.first;
    for (const auto& t : a.second.resource_map) {  // type
      labels.resource_type = t.first->type_url();
      // Count the number of entries in each state.
      std::map<absl::string_view, uint64_t> counts;
      for (const auto& r : t.second) {  // resource id
        absl::string_view cache_state =
            CacheStateForEntry(r.second.meta, r.second.resource != nullptr);
        ++counts[cache_state];
      }
      // Report the count for each state.
      for (const auto& c : counts) {
        labels.cache_state = c.first;
        func(labels, c.second);
      }
    }
  }
}

void XdsClient::ReportServerConnections(
    absl::FunctionRef<void(absl::string_view, bool)> func) {
  for (const auto& p : xds_channel_map_) {
    func(p.second->server_uri(), p.second->status().ok());
  }
}

}  // namespace grpc_core
