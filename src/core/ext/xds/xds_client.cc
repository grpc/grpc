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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_client.h"

#include <inttypes.h>
#include <string.h>

#include <algorithm>
#include <type_traits>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "upb/mem/arena.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/log.h>

#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/uri/uri_parser.h"

#define GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_XDS_RECONNECT_JITTER 0.2
#define GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS 1000

namespace grpc_core {

using ::grpc_event_engine::experimental::EventEngine;

TraceFlag grpc_xds_client_trace(false, "xds_client");
TraceFlag grpc_xds_client_refcount_trace(false, "xds_client_refcount");

//
// Internal class declarations
//

// An xds call wrapper that can restart a call upon failure. Holds a ref to
// the xds channel. The template parameter is the kind of wrapped xds call.
template <typename T>
class XdsClient::ChannelState::RetryableCall
    : public InternallyRefCounted<RetryableCall<T>> {
 public:
  explicit RetryableCall(WeakRefCountedPtr<ChannelState> chand);

  // Disable thread-safety analysis because this method is called via
  // OrphanablePtr<>, but there's no way to pass the lock annotation
  // through there.
  void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

  void OnCallFinishedLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  T* calld() const { return calld_.get(); }
  ChannelState* chand() const { return chand_.get(); }

  bool IsCurrentCallOnChannel() const;

 private:
  void StartNewCallLocked();
  void StartRetryTimerLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  void OnRetryTimer();

  // The wrapped xds call that talks to the xds server. It's instantiated
  // every time we start a new call. It's null during call retry backoff.
  OrphanablePtr<T> calld_;
  // The owning xds channel.
  WeakRefCountedPtr<ChannelState> chand_;

  // Retry state.
  BackOff backoff_;
  absl::optional<EventEngine::TaskHandle> timer_handle_
      ABSL_GUARDED_BY(&XdsClient::mu_);

  bool shutting_down_ = false;
};

// Contains an ADS call to the xds server.
class XdsClient::ChannelState::AdsCallState
    : public InternallyRefCounted<AdsCallState> {
 public:
  // The ctor and dtor should not be used directly.
  explicit AdsCallState(RefCountedPtr<RetryableCall<AdsCallState>> parent);

  void Orphan() override;

  RetryableCall<AdsCallState>* parent() const { return parent_.get(); }
  ChannelState* chand() const { return parent_->chand(); }
  XdsClient* xds_client() const { return chand()->xds_client(); }
  bool seen_response() const { return seen_response_; }

  void SubscribeLocked(const XdsResourceType* type, const XdsResourceName& name,
                       bool delay_send)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void UnsubscribeLocked(const XdsResourceType* type,
                         const XdsResourceName& name, bool delay_unsubscription)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  bool HasSubscribedResources() const;

 private:
  class AdsResponseParser : public XdsApi::AdsResponseParserInterface {
   public:
    struct Result {
      const XdsResourceType* type;
      std::string type_url;
      std::string version;
      std::string nonce;
      std::vector<std::string> errors;
      std::map<std::string /*authority*/, std::set<XdsResourceKey>>
          resources_seen;
      bool have_valid_resources = false;
    };

    explicit AdsResponseParser(AdsCallState* ads_call_state)
        : ads_call_state_(ads_call_state) {}

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
    XdsClient* xds_client() const { return ads_call_state_->xds_client(); }

    AdsCallState* ads_call_state_;
    const Timestamp update_time_ = Timestamp::Now();
    Result result_;
  };

  class ResourceTimer : public InternallyRefCounted<ResourceTimer> {
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

    void MaybeMarkSubscriptionSendComplete(
        RefCountedPtr<AdsCallState> ads_calld)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      if (subscription_sent_) MaybeStartTimer(std::move(ads_calld));
    }

    void MarkSeen() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      resource_seen_ = true;
      MaybeCancelTimer();
    }

    void MaybeCancelTimer() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      if (timer_handle_.has_value() &&
          ads_calld_->xds_client()->engine()->Cancel(*timer_handle_)) {
        timer_handle_.reset();
      }
    }

   private:
    void MaybeStartTimer(RefCountedPtr<AdsCallState> ads_calld)
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
          ads_calld->xds_client()->authority_state_map_[name_.authority];
      ResourceState& state = authority_state.resource_map[type_][name_.key];
      if (state.resource != nullptr) return;
      // Start timer.
      ads_calld_ = std::move(ads_calld);
      timer_handle_ = ads_calld_->xds_client()->engine()->RunAfter(
          ads_calld_->xds_client()->request_timeout_,
          [self = Ref(DEBUG_LOCATION, "timer")]() {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            self->OnTimer();
          });
    }

    void OnTimer() {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] xds server %s: timeout obtaining resource "
                "{type=%s name=%s} from xds server",
                ads_calld_->xds_client(),
                ads_calld_->chand()->server_.server_uri().c_str(),
                std::string(type_->type_url()).c_str(),
                XdsClient::ConstructFullXdsResourceName(
                    name_.authority, type_->type_url(), name_.key)
                    .c_str());
      }
      {
        MutexLock lock(&ads_calld_->xds_client()->mu_);
        timer_handle_.reset();
        resource_seen_ = true;
        auto& authority_state =
            ads_calld_->xds_client()->authority_state_map_[name_.authority];
        ResourceState& state = authority_state.resource_map[type_][name_.key];
        state.meta.client_status = XdsApi::ResourceMetadata::DOES_NOT_EXIST;
        ads_calld_->xds_client()->NotifyWatchersOnResourceDoesNotExist(
            state.watchers);
      }
      ads_calld_.reset();
    }

    const XdsResourceType* type_;
    const XdsResourceName name_;

    RefCountedPtr<AdsCallState> ads_calld_;
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

  class StreamEventHandler
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(RefCountedPtr<AdsCallState> ads_calld)
        : ads_calld_(std::move(ads_calld)) {}

    void OnRequestSent(bool ok) override { ads_calld_->OnRequestSent(ok); }
    void OnRecvMessage(absl::string_view payload) override {
      ads_calld_->OnRecvMessage(payload);
    }
    void OnStatusReceived(absl::Status status) override {
      ads_calld_->OnStatusReceived(std::move(status));
    }

   private:
    RefCountedPtr<AdsCallState> ads_calld_;
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
  RefCountedPtr<RetryableCall<AdsCallState>> parent_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall> call_;

  bool sent_initial_message_ = false;
  bool seen_response_ = false;

  const XdsResourceType* send_message_pending_
      ABSL_GUARDED_BY(&XdsClient::mu_) = nullptr;

  // Resource types for which requests need to be sent.
  std::set<const XdsResourceType*> buffered_requests_;

  // State for each resource type.
  std::map<const XdsResourceType*, ResourceTypeState> state_map_;
};

// Contains an LRS call to the xds server.
class XdsClient::ChannelState::LrsCallState
    : public InternallyRefCounted<LrsCallState> {
 public:
  // The ctor and dtor should not be used directly.
  explicit LrsCallState(RefCountedPtr<RetryableCall<LrsCallState>> parent);

  void Orphan() override;

  void MaybeStartReportingLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  RetryableCall<LrsCallState>* parent() { return parent_.get(); }
  ChannelState* chand() const { return parent_->chand(); }
  XdsClient* xds_client() const { return chand()->xds_client(); }
  bool seen_response() const { return seen_response_; }

 private:
  class StreamEventHandler
      : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
   public:
    explicit StreamEventHandler(RefCountedPtr<LrsCallState> lrs_calld)
        : lrs_calld_(std::move(lrs_calld)) {}

    void OnRequestSent(bool ok) override { lrs_calld_->OnRequestSent(ok); }
    void OnRecvMessage(absl::string_view payload) override {
      lrs_calld_->OnRecvMessage(payload);
    }
    void OnStatusReceived(absl::Status status) override {
      lrs_calld_->OnStatusReceived(std::move(status));
    }

   private:
    RefCountedPtr<LrsCallState> lrs_calld_;
  };

  // Reports client-side load stats according to a fixed interval.
  class Reporter : public InternallyRefCounted<Reporter> {
   public:
    Reporter(RefCountedPtr<LrsCallState> parent, Duration report_interval)
        : parent_(std::move(parent)), report_interval_(report_interval) {
      ScheduleNextReportLocked();
    }

    // Disable thread-safety analysis because this method is called via
    // OrphanablePtr<>, but there's no way to pass the lock annotation
    // through there.
    void Orphan() override ABSL_NO_THREAD_SAFETY_ANALYSIS;

    void OnReportDoneLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

   private:
    void ScheduleNextReportLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    bool OnNextReportTimer();
    bool SendReportLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

    bool IsCurrentReporterOnCall() const {
      return this == parent_->reporter_.get();
    }
    XdsClient* xds_client() const { return parent_->xds_client(); }

    // The owning LRS call.
    RefCountedPtr<LrsCallState> parent_;

    // The load reporting state.
    const Duration report_interval_;
    bool last_report_counters_were_zero_ = false;
    absl::optional<EventEngine::TaskHandle> timer_handle_
        ABSL_GUARDED_BY(&XdsClient::mu_);
  };

  void OnRequestSent(bool ok);
  void OnRecvMessage(absl::string_view payload);
  void OnStatusReceived(absl::Status status);

  bool IsCurrentCallOnChannel() const;

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<LrsCallState>> parent_;

  OrphanablePtr<XdsTransportFactory::XdsTransport::StreamingCall> call_;

  bool seen_response_ = false;
  bool send_message_pending_ ABSL_GUARDED_BY(&XdsClient::mu_) = false;

  // Load reporting state.
  bool send_all_clusters_ = false;
  std::set<std::string> cluster_names_;  // Asked for by the LRS server.
  Duration load_reporting_interval_;
  OrphanablePtr<Reporter> reporter_;
};

//
// XdsClient::ChannelState
//

XdsClient::ChannelState::ChannelState(WeakRefCountedPtr<XdsClient> xds_client,
                                      const XdsBootstrap::XdsServer& server)
    : DualRefCounted<ChannelState>(
          GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace)
              ? "ChannelState"
              : nullptr),
      xds_client_(std::move(xds_client)),
      server_(server) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] creating channel %p for server %s",
            xds_client_.get(), this, server.server_uri().c_str());
  }
  absl::Status status;
  transport_ = xds_client_->transport_factory_->Create(
      server,
      [self = WeakRef(DEBUG_LOCATION, "OnConnectivityFailure")](
          absl::Status status) {
        self->OnConnectivityFailure(std::move(status));
      },
      &status);
  GPR_ASSERT(transport_ != nullptr);
  if (!status.ok()) SetChannelStatusLocked(std::move(status));
}

XdsClient::ChannelState::~ChannelState() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] destroying xds channel %p for server %s",
            xds_client(), this, server_.server_uri().c_str());
  }
  xds_client_.reset(DEBUG_LOCATION, "ChannelState");
}

// This method should only ever be called when holding the lock, but we can't
// use a ABSL_EXCLUSIVE_LOCKS_REQUIRED annotation, because Orphan() will be
// called from DualRefCounted::Unref, which cannot have a lock annotation for
// a lock in this subclass.
void XdsClient::ChannelState::Orphan() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] orphaning xds channel %p for server %s",
            xds_client(), this, server_.server_uri().c_str());
  }
  shutting_down_ = true;
  transport_.reset();
  // At this time, all strong refs are removed, remove from channel map to
  // prevent subsequent subscription from trying to use this ChannelState as
  // it is shutting down.
  xds_client_->xds_server_channel_map_.erase(&server_);
  ads_calld_.reset();
  lrs_calld_.reset();
}

void XdsClient::ChannelState::ResetBackoff() { transport_->ResetBackoff(); }

XdsClient::ChannelState::AdsCallState* XdsClient::ChannelState::ads_calld()
    const {
  return ads_calld_->calld();
}

XdsClient::ChannelState::LrsCallState* XdsClient::ChannelState::lrs_calld()
    const {
  return lrs_calld_->calld();
}

void XdsClient::ChannelState::MaybeStartLrsCall() {
  if (lrs_calld_ != nullptr) return;
  lrs_calld_.reset(new RetryableCall<LrsCallState>(
      WeakRef(DEBUG_LOCATION, "ChannelState+lrs")));
}

void XdsClient::ChannelState::StopLrsCallLocked() {
  xds_client_->xds_load_report_server_map_.erase(&server_);
  lrs_calld_.reset();
}

void XdsClient::ChannelState::SubscribeLocked(const XdsResourceType* type,
                                              const XdsResourceName& name) {
  if (ads_calld_ == nullptr) {
    // Start the ADS call if this is the first request.
    ads_calld_.reset(new RetryableCall<AdsCallState>(
        WeakRef(DEBUG_LOCATION, "ChannelState+ads")));
    // Note: AdsCallState's ctor will automatically subscribe to all
    // resources that the XdsClient already has watchers for, so we can
    // return here.
    return;
  }
  // If the ADS call is in backoff state, we don't need to do anything now
  // because when the call is restarted it will resend all necessary requests.
  if (ads_calld() == nullptr) return;
  // Subscribe to this resource if the ADS call is active.
  ads_calld()->SubscribeLocked(type, name, /*delay_send=*/false);
}

void XdsClient::ChannelState::UnsubscribeLocked(const XdsResourceType* type,
                                                const XdsResourceName& name,
                                                bool delay_unsubscription) {
  if (ads_calld_ != nullptr) {
    auto* calld = ads_calld_->calld();
    if (calld != nullptr) {
      calld->UnsubscribeLocked(type, name, delay_unsubscription);
      if (!calld->HasSubscribedResources()) {
        ads_calld_.reset();
      }
    }
  }
}

void XdsClient::ChannelState::OnConnectivityFailure(absl::Status status) {
  MutexLock lock(&xds_client_->mu_);
  SetChannelStatusLocked(std::move(status));
}

void XdsClient::ChannelState::SetChannelStatusLocked(absl::Status status) {
  if (shutting_down_) return;
  status = absl::Status(status.code(), absl::StrCat("xDS channel for server ",
                                                    server_.server_uri(), ": ",
                                                    status.message()));
  gpr_log(GPR_INFO, "[xds_client %p] %s", xds_client(),
          status.ToString().c_str());
  // If the node ID is set, append that to the status message that we send to
  // the watchers, so that it will appear in log messages visible to users.
  const auto* node = xds_client_->bootstrap_->node();
  if (node != nullptr) {
    status = absl::Status(
        status.code(),
        absl::StrCat(status.message(),
                     " (node ID:", xds_client_->bootstrap_->node()->id(), ")"));
  }
  // Save status in channel, so that we can immediately generate an
  // error for any new watchers that may be started.
  status_ = status;
  // Find all watchers for this channel.
  std::set<RefCountedPtr<ResourceWatcherInterface>> watchers;
  for (const auto& a : xds_client_->authority_state_map_) {  // authority
    if (a.second.channel_state != this) continue;
    for (const auto& t : a.second.resource_map) {  // type
      for (const auto& r : t.second) {             // resource id
        for (const auto& w : r.second.watchers) {  // watchers
          watchers.insert(w.second);
        }
      }
    }
  }
  // Enqueue notification for the watchers.
  xds_client_->work_serializer_.Run(
      [watchers = std::move(watchers), status = std::move(status)]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(xds_client_->work_serializer_) {
            for (const auto& watcher : watchers) {
              watcher->OnError(status);
            }
          });
}

//
// XdsClient::ChannelState::RetryableCall<>
//

template <typename T>
XdsClient::ChannelState::RetryableCall<T>::RetryableCall(
    WeakRefCountedPtr<ChannelState> chand)
    : chand_(std::move(chand)),
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
void XdsClient::ChannelState::RetryableCall<T>::Orphan() {
  shutting_down_ = true;
  calld_.reset();
  if (timer_handle_.has_value()) {
    chand()->xds_client()->engine()->Cancel(*timer_handle_);
    timer_handle_.reset();
  }
  this->Unref(DEBUG_LOCATION, "RetryableCall+orphaned");
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnCallFinishedLocked() {
  // If we saw a response on the current stream, reset backoff.
  if (calld_->seen_response()) backoff_.Reset();
  calld_.reset();
  // Start retry timer.
  StartRetryTimerLocked();
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::StartNewCallLocked() {
  if (shutting_down_) return;
  GPR_ASSERT(chand_->transport_ != nullptr);
  GPR_ASSERT(calld_ == nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: start new call from retryable "
            "call %p",
            chand()->xds_client(), chand()->server_.server_uri().c_str(), this);
  }
  calld_ = MakeOrphanable<T>(
      this->Ref(DEBUG_LOCATION, "RetryableCall+start_new_call"));
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::StartRetryTimerLocked() {
  if (shutting_down_) return;
  const Timestamp next_attempt_time = backoff_.NextAttemptTime();
  const Duration timeout =
      std::max(next_attempt_time - Timestamp::Now(), Duration::Zero());
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: call attempt failed; "
            "retry timer will fire in %" PRId64 "ms.",
            chand()->xds_client(), chand()->server_.server_uri().c_str(),
            timeout.millis());
  }
  timer_handle_ = chand()->xds_client()->engine()->RunAfter(
      timeout,
      [self = this->Ref(DEBUG_LOCATION, "RetryableCall+retry_timer_start")]() {
        ApplicationCallbackExecCtx callback_exec_ctx;
        ExecCtx exec_ctx;
        self->OnRetryTimer();
      });
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnRetryTimer() {
  MutexLock lock(&chand_->xds_client()->mu_);
  if (timer_handle_.has_value()) {
    timer_handle_.reset();
    if (shutting_down_) return;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] xds server %s: retry timer fired (retryable "
              "call: %p)",
              chand()->xds_client(), chand()->server_.server_uri().c_str(),
              this);
    }
    StartNewCallLocked();
  }
}

//
// XdsClient::ChannelState::AdsCallState::AdsResponseParser
//

absl::Status XdsClient::ChannelState::AdsCallState::AdsResponseParser::
    ProcessAdsResponseFields(AdsResponseFields fields) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_client %p] xds server %s: received ADS response: type_url=%s, "
        "version=%s, nonce=%s, num_resources=%" PRIuPTR,
        ads_call_state_->xds_client(),
        ads_call_state_->chand()->server_.server_uri().c_str(),
        fields.type_url.c_str(), fields.version.c_str(), fields.nonce.c_str(),
        fields.num_resources);
  }
  result_.type =
      ads_call_state_->xds_client()->GetResourceTypeLocked(fields.type_url);
  if (result_.type == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown resource type ", fields.type_url));
  }
  result_.type_url = std::move(fields.type_url);
  result_.version = std::move(fields.version);
  result_.nonce = std::move(fields.nonce);
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

void XdsClient::ChannelState::AdsCallState::AdsResponseParser::ParseResource(
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
    return;
  }
  // Parse the resource.
  XdsResourceType::DecodeContext context = {
      xds_client(), ads_call_state_->chand()->server_, &grpc_xds_client_trace,
      xds_client()->symtab_.ptr(), arena};
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
    return;
  }
  // Cancel resource-does-not-exist timer, if needed.
  auto timer_it = ads_call_state_->state_map_.find(result_.type);
  if (timer_it != ads_call_state_->state_map_.end()) {
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
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: server returned new version of "
            "resource for which we previously ignored a deletion: type %s "
            "name %s",
            xds_client(),
            ads_call_state_->chand()->server_.server_uri().c_str(),
            std::string(type_url).c_str(), std::string(resource_name).c_str());
    resource_state.ignored_deletion = false;
  }
  // Update resource state based on whether the resource is valid.
  if (!decode_status.ok()) {
    xds_client()->NotifyWatchersOnErrorLocked(
        resource_state.watchers,
        absl::UnavailableError(
            absl::StrCat("invalid resource: ", decode_status.ToString())));
    UpdateResourceMetadataNacked(result_.version, decode_status.ToString(),
                                 update_time_, &resource_state.meta);
    return;
  }
  // Resource is valid.
  result_.have_valid_resources = true;
  // If it didn't change, ignore it.
  if (resource_state.resource != nullptr &&
      result_.type->ResourcesEqual(resource_state.resource.get(),
                                   decode_result.resource->get())) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] %s resource %s identical to current, ignoring.",
              xds_client(), result_.type_url.c_str(),
              std::string(resource_name).c_str());
    }
    return;
  }
  // Update the resource state.
  resource_state.resource = std::move(*decode_result.resource);
  resource_state.meta = CreateResourceMetadataAcked(
      std::string(serialized_resource), result_.version, update_time_);
  // Notify watchers.
  auto& watchers_list = resource_state.watchers;
  xds_client()->work_serializer_.Run(
      [watchers_list, value = resource_state.resource]()
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(&xds_client()->work_serializer_) {
            for (const auto& p : watchers_list) {
              p.first->OnGenericResourceChanged(value);
            }
          });
}

void XdsClient::ChannelState::AdsCallState::AdsResponseParser::
    ResourceWrapperParsingFailed(size_t idx, absl::string_view message) {
  result_.errors.emplace_back(
      absl::StrCat("resource index ", idx, ": ", message));
}

//
// XdsClient::ChannelState::AdsCallState
//

XdsClient::ChannelState::AdsCallState::AdsCallState(
    RefCountedPtr<RetryableCall<AdsCallState>> parent)
    : InternallyRefCounted<AdsCallState>(
          GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace)
              ? "AdsCallState"
              : nullptr),
      parent_(std::move(parent)) {
  GPR_ASSERT(xds_client() != nullptr);
  // Init the ADS call.
  const char* method =
      "/envoy.service.discovery.v3.AggregatedDiscoveryService/"
      "StreamAggregatedResources";
  call_ = chand()->transport_->CreateStreamingCall(
      method, std::make_unique<StreamEventHandler>(
                  // Passing the initial ref here.  This ref will go away when
                  // the StreamEventHandler is destroyed.
                  RefCountedPtr<AdsCallState>(this)));
  GPR_ASSERT(call_ != nullptr);
  // Start the call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: starting ADS call "
            "(calld: %p, call: %p)",
            xds_client(), chand()->server_.server_uri().c_str(), this,
            call_.get());
  }
  // If this is a reconnect, add any necessary subscriptions from what's
  // already in the cache.
  for (const auto& a : xds_client()->authority_state_map_) {
    const std::string& authority = a.first;
    // Skip authorities that are not using this xDS channel.
    if (a.second.channel_state != chand()) continue;
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
}

void XdsClient::ChannelState::AdsCallState::Orphan() {
  state_map_.clear();
  // Note that the initial ref is held by the StreamEventHandler, which
  // will be destroyed when call_ is destroyed, which may not happen
  // here, since there may be other refs held to call_ by internal callbacks.
  call_.reset();
}

void XdsClient::ChannelState::AdsCallState::SendMessageLocked(
    const XdsResourceType* type)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
  // Buffer message sending if an existing message is in flight.
  if (send_message_pending_ != nullptr) {
    buffered_requests_.insert(type);
    return;
  }
  auto& state = state_map_[type];
  std::string serialized_message = xds_client()->api_.CreateAdsRequest(
      type->type_url(), chand()->resource_type_version_map_[type], state.nonce,
      ResourceNamesForRequest(type), state.status, !sent_initial_message_);
  sent_initial_message_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: sending ADS request: type=%s "
            "version=%s nonce=%s error=%s",
            xds_client(), chand()->server_.server_uri().c_str(),
            std::string(type->type_url()).c_str(),
            chand()->resource_type_version_map_[type].c_str(),
            state.nonce.c_str(), state.status.ToString().c_str());
  }
  state.status = absl::OkStatus();
  call_->SendMessage(std::move(serialized_message));
  send_message_pending_ = type;
}

void XdsClient::ChannelState::AdsCallState::SubscribeLocked(
    const XdsResourceType* type, const XdsResourceName& name, bool delay_send) {
  auto& state = state_map_[type].subscribed_resources[name.authority][name.key];
  if (state == nullptr) {
    state = MakeOrphanable<ResourceTimer>(type, name);
    if (!delay_send) SendMessageLocked(type);
  }
}

void XdsClient::ChannelState::AdsCallState::UnsubscribeLocked(
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

bool XdsClient::ChannelState::AdsCallState::HasSubscribedResources() const {
  for (const auto& p : state_map_) {
    if (!p.second.subscribed_resources.empty()) return true;
  }
  return false;
}

void XdsClient::ChannelState::AdsCallState::OnRequestSent(bool ok) {
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

void XdsClient::ChannelState::AdsCallState::OnRecvMessage(
    absl::string_view payload) {
  MutexLock lock(&xds_client()->mu_);
  if (!IsCurrentCallOnChannel()) return;
  // Parse and validate the response.
  AdsResponseParser parser(this);
  absl::Status status = xds_client()->api_.ParseAdsResponse(payload, &parser);
  if (!status.ok()) {
    // Ignore unparsable response.
    gpr_log(GPR_ERROR,
            "[xds_client %p] xds server %s: error parsing ADS response (%s) "
            "-- ignoring",
            xds_client(), chand()->server_.server_uri().c_str(),
            status.ToString().c_str());
  } else {
    seen_response_ = true;
    chand()->status_ = absl::OkStatus();
    AdsResponseParser::Result result = parser.TakeResult();
    // Update nonce.
    auto& state = state_map_[result.type];
    state.nonce = result.nonce;
    // If we got an error, set state.status so that we'll NACK the update.
    if (!result.errors.empty()) {
      state.status = absl::UnavailableError(
          absl::StrCat("xDS response validation errors: [",
                       absl::StrJoin(result.errors, "; "), "]"));
      gpr_log(GPR_ERROR,
              "[xds_client %p] xds server %s: ADS response invalid for "
              "resource "
              "type %s version %s, will NACK: nonce=%s status=%s",
              xds_client(), chand()->server_.server_uri().c_str(),
              result.type_url.c_str(), result.version.c_str(),
              state.nonce.c_str(), state.status.ToString().c_str());
    }
    // Delete resources not seen in update if needed.
    if (result.type->AllResourcesRequiredInSotW()) {
      for (auto& a : xds_client()->authority_state_map_) {
        const std::string& authority = a.first;
        AuthorityState& authority_state = a.second;
        // Skip authorities that are not using this xDS channel.
        if (authority_state.channel_state != chand()) continue;
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
            if (chand()->server_.IgnoreResourceDeletion()) {
              if (!resource_state.ignored_deletion) {
                gpr_log(GPR_ERROR,
                        "[xds_client %p] xds server %s: ignoring deletion "
                        "for resource type %s name %s",
                        xds_client(), chand()->server_.server_uri().c_str(),
                        result.type_url.c_str(),
                        XdsClient::ConstructFullXdsResourceName(
                            authority, result.type_url, resource_key)
                            .c_str());
                resource_state.ignored_deletion = true;
              }
            } else {
              resource_state.resource.reset();
              resource_state.meta.client_status =
                  XdsApi::ResourceMetadata::DOES_NOT_EXIST;
              xds_client()->NotifyWatchersOnResourceDoesNotExist(
                  resource_state.watchers);
            }
          }
        }
      }
    }
    // If we had valid resources or the update was empty, update the version.
    if (result.have_valid_resources || result.errors.empty()) {
      chand()->resource_type_version_map_[result.type] =
          std::move(result.version);
      // Start load reporting if needed.
      auto& lrs_call = chand()->lrs_calld_;
      if (lrs_call != nullptr) {
        LrsCallState* lrs_calld = lrs_call->calld();
        if (lrs_calld != nullptr) lrs_calld->MaybeStartReportingLocked();
      }
    }
    // Send ACK or NACK.
    SendMessageLocked(result.type);
  }
}

void XdsClient::ChannelState::AdsCallState::OnStatusReceived(
    absl::Status status) {
  MutexLock lock(&xds_client()->mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: ADS call status received "
            "(chand=%p, ads_calld=%p, call=%p): %s",
            xds_client(), chand()->server_.server_uri().c_str(), chand(), this,
            call_.get(), status.ToString().c_str());
  }
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
    parent_->OnCallFinishedLocked();
    // If we didn't receive a response on the stream, report the
    // stream failure as a connectivity failure, which will report the
    // error to all watchers of resources on this channel.
    if (!seen_response_) {
      chand()->SetChannelStatusLocked(absl::UnavailableError(
          absl::StrCat("xDS call failed with no responses received; status: ",
                       status.ToString())));
    }
  }
}

bool XdsClient::ChannelState::AdsCallState::IsCurrentCallOnChannel() const {
  // If the retryable ADS call is null (which only happens when the xds
  // channel is shutting down), all the ADS calls are stale.
  if (chand()->ads_calld_ == nullptr) return false;
  return this == chand()->ads_calld_->calld();
}

std::vector<std::string>
XdsClient::ChannelState::AdsCallState::ResourceNamesForRequest(
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
// XdsClient::ChannelState::LrsCallState::Reporter
//

void XdsClient::ChannelState::LrsCallState::Reporter::Orphan() {
  if (timer_handle_.has_value() &&
      xds_client()->engine()->Cancel(*timer_handle_)) {
    timer_handle_.reset();
    Unref(DEBUG_LOCATION, "Orphan");
  }
}

void XdsClient::ChannelState::LrsCallState::Reporter::
    ScheduleNextReportLocked() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: scheduling load report timer",
            xds_client(), parent_->chand()->server_.server_uri().c_str());
  }
  timer_handle_ = xds_client()->engine()->RunAfter(report_interval_, [this]() {
    ApplicationCallbackExecCtx callback_exec_ctx;
    ExecCtx exec_ctx;
    if (OnNextReportTimer()) {
      Unref(DEBUG_LOCATION, "OnNextReportTimer()");
    }
  });
}

bool XdsClient::ChannelState::LrsCallState::Reporter::OnNextReportTimer() {
  MutexLock lock(&xds_client()->mu_);
  timer_handle_.reset();
  if (!IsCurrentReporterOnCall()) return true;
  SendReportLocked();
  return false;
}

namespace {

bool LoadReportCountersAreZero(const XdsApi::ClusterLoadReportMap& snapshot) {
  for (const auto& p : snapshot) {
    const XdsApi::ClusterLoadReport& cluster_snapshot = p.second;
    if (!cluster_snapshot.dropped_requests.IsZero()) return false;
    for (const auto& q : cluster_snapshot.locality_stats) {
      const XdsClusterLocalityStats::Snapshot& locality_snapshot = q.second;
      if (!locality_snapshot.IsZero()) return false;
    }
  }
  return true;
}

}  // namespace

bool XdsClient::ChannelState::LrsCallState::Reporter::SendReportLocked() {
  // Construct snapshot from all reported stats.
  XdsApi::ClusterLoadReportMap snapshot =
      xds_client()->BuildLoadReportSnapshotLocked(parent_->chand()->server_,
                                                  parent_->send_all_clusters_,
                                                  parent_->cluster_names_);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  const bool old_val = last_report_counters_were_zero_;
  last_report_counters_were_zero_ = LoadReportCountersAreZero(snapshot);
  if (old_val && last_report_counters_were_zero_) {
    auto it = xds_client()->xds_load_report_server_map_.find(
        &parent_->chand()->server_);
    if (it == xds_client()->xds_load_report_server_map_.end() ||
        it->second.load_report_map.empty()) {
      it->second.channel_state->StopLrsCallLocked();
      return true;
    }
    ScheduleNextReportLocked();
    return false;
  }
  // Send a request that contains the snapshot.
  std::string serialized_payload =
      xds_client()->api_.CreateLrsRequest(std::move(snapshot));
  parent_->call_->SendMessage(std::move(serialized_payload));
  parent_->send_message_pending_ = true;
  return false;
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnReportDoneLocked() {
  // If a reporter starts a send_message op, then the reporting interval
  // changes and we destroy that reporter and create a new one, and then
  // the send_message op started by the old reporter finishes, this
  // method will be called even though it was for a completion started
  // by the old reporter.  In that case, the timer will be pending, so
  // we just ignore the completion and wait for the timer to fire.
  if (timer_handle_.has_value()) return;
  // If there are no more registered stats to report, cancel the call.
  auto it = xds_client()->xds_load_report_server_map_.find(
      &parent_->chand()->server_);
  if (it == xds_client()->xds_load_report_server_map_.end()) return;
  if (it->second.load_report_map.empty()) {
    if (it->second.channel_state != nullptr) {
      it->second.channel_state->StopLrsCallLocked();
    }
    return;
  }
  // Otherwise, schedule the next load report.
  ScheduleNextReportLocked();
}

//
// XdsClient::ChannelState::LrsCallState
//

XdsClient::ChannelState::LrsCallState::LrsCallState(
    RefCountedPtr<RetryableCall<LrsCallState>> parent)
    : InternallyRefCounted<LrsCallState>(
          GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace)
              ? "LrsCallState"
              : nullptr),
      parent_(std::move(parent)) {
  // Init the LRS call. Note that the call will progress every time there's
  // activity in xds_client()->interested_parties_, which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(xds_client() != nullptr);
  const char* method =
      "/envoy.service.load_stats.v3.LoadReportingService/StreamLoadStats";
  call_ = chand()->transport_->CreateStreamingCall(
      method, std::make_unique<StreamEventHandler>(
                  // Passing the initial ref here.  This ref will go away when
                  // the StreamEventHandler is destroyed.
                  RefCountedPtr<LrsCallState>(this)));
  GPR_ASSERT(call_ != nullptr);
  // Start the call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: starting LRS call (calld=%p, "
            "call=%p)",
            xds_client(), chand()->server_.server_uri().c_str(), this,
            call_.get());
  }
  // Send the initial request.
  std::string serialized_payload = xds_client()->api_.CreateLrsInitialRequest();
  call_->SendMessage(std::move(serialized_payload));
  send_message_pending_ = true;
}

void XdsClient::ChannelState::LrsCallState::Orphan() {
  reporter_.reset();
  // Note that the initial ref is held by the StreamEventHandler, which
  // will be destroyed when call_ is destroyed, which may not happen
  // here, since there may be other refs held to call_ by internal callbacks.
  call_.reset();
}

void XdsClient::ChannelState::LrsCallState::MaybeStartReportingLocked() {
  // Don't start again if already started.
  if (reporter_ != nullptr) return;
  // Don't start if the previous send_message op (of the initial request or
  // the last report of the previous reporter) hasn't completed.
  if (call_ != nullptr && send_message_pending_) return;
  // Don't start if no LRS response has arrived.
  if (!seen_response()) return;
  // Don't start if the ADS call hasn't received any valid response. Note that
  // this must be the first channel because it is the current channel but its
  // ADS call hasn't seen any response.
  if (chand()->ads_calld_ == nullptr ||
      chand()->ads_calld_->calld() == nullptr ||
      !chand()->ads_calld_->calld()->seen_response()) {
    return;
  }
  // Start reporting.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] xds server %s: creating load reporter",
            xds_client(), chand()->server_.server_uri().c_str());
  }
  reporter_ = MakeOrphanable<Reporter>(
      Ref(DEBUG_LOCATION, "LRS+load_report+start"), load_reporting_interval_);
}

void XdsClient::ChannelState::LrsCallState::OnRequestSent(bool /*ok*/) {
  MutexLock lock(&xds_client()->mu_);
  send_message_pending_ = false;
  if (reporter_ != nullptr) {
    reporter_->OnReportDoneLocked();
  } else {
    MaybeStartReportingLocked();
  }
}

void XdsClient::ChannelState::LrsCallState::OnRecvMessage(
    absl::string_view payload) {
  MutexLock lock(&xds_client()->mu_);
  // If we're no longer the current call, ignore the result.
  if (!IsCurrentCallOnChannel()) return;
  // Parse the response.
  bool send_all_clusters = false;
  std::set<std::string> new_cluster_names;
  Duration new_load_reporting_interval;
  absl::Status status = xds_client()->api_.ParseLrsResponse(
      payload, &send_all_clusters, &new_cluster_names,
      &new_load_reporting_interval);
  if (!status.ok()) {
    gpr_log(GPR_ERROR,
            "[xds_client %p] xds server %s: LRS response parsing failed: %s",
            xds_client(), chand()->server_.server_uri().c_str(),
            status.ToString().c_str());
    return;
  }
  seen_response_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(
        GPR_INFO,
        "[xds_client %p] xds server %s: LRS response received, %" PRIuPTR
        " cluster names, send_all_clusters=%d, load_report_interval=%" PRId64
        "ms",
        xds_client(), chand()->server_.server_uri().c_str(),
        new_cluster_names.size(), send_all_clusters,
        new_load_reporting_interval.millis());
    size_t i = 0;
    for (const auto& name : new_cluster_names) {
      gpr_log(GPR_INFO, "[xds_client %p] cluster_name %" PRIuPTR ": %s",
              xds_client(), i++, name.c_str());
    }
  }
  if (new_load_reporting_interval <
      Duration::Milliseconds(GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS)) {
    new_load_reporting_interval =
        Duration::Milliseconds(GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] xds server %s: increased load_report_interval "
              "to minimum value %dms",
              xds_client(), chand()->server_.server_uri().c_str(),
              GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS);
    }
  }
  // Ignore identical update.
  if (send_all_clusters == send_all_clusters_ &&
      cluster_names_ == new_cluster_names &&
      load_reporting_interval_ == new_load_reporting_interval) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] xds server %s: incoming LRS response identical "
              "to current, ignoring.",
              xds_client(), chand()->server_.server_uri().c_str());
    }
    return;
  }
  // Stop current load reporting (if any) to adopt the new config.
  reporter_.reset();
  // Record the new config.
  send_all_clusters_ = send_all_clusters;
  cluster_names_ = std::move(new_cluster_names);
  load_reporting_interval_ = new_load_reporting_interval;
  // Try starting sending load report.
  MaybeStartReportingLocked();
}

void XdsClient::ChannelState::LrsCallState::OnStatusReceived(
    absl::Status status) {
  MutexLock lock(&xds_client()->mu_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] xds server %s: LRS call status received "
            "(chand=%p, calld=%p, call=%p): %s",
            xds_client(), chand()->server_.server_uri().c_str(), chand(), this,
            call_.get(), status.ToString().c_str());
  }
  // Ignore status from a stale call.
  if (IsCurrentCallOnChannel()) {
    // Try to restart the call.
    parent_->OnCallFinishedLocked();
  }
}

bool XdsClient::ChannelState::LrsCallState::IsCurrentCallOnChannel() const {
  // If the retryable LRS call is null (which only happens when the xds
  // channel is shutting down), all the LRS calls are stale.
  if (chand()->lrs_calld_ == nullptr) return false;
  return this == chand()->lrs_calld_->calld();
}

//
// XdsClient
//

XdsClient::XdsClient(
    std::unique_ptr<XdsBootstrap> bootstrap,
    OrphanablePtr<XdsTransportFactory> transport_factory,
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine,
    std::string user_agent_name, std::string user_agent_version,
    Duration resource_request_timeout)
    : DualRefCounted<XdsClient>(
          GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace) ? "XdsClient"
                                                                  : nullptr),
      bootstrap_(std::move(bootstrap)),
      transport_factory_(std::move(transport_factory)),
      request_timeout_(resource_request_timeout),
      xds_federation_enabled_(XdsFederationEnabled()),
      api_(this, &grpc_xds_client_trace, bootstrap_->node(), &symtab_,
           std::move(user_agent_name), std::move(user_agent_version)),
      work_serializer_(engine),
      engine_(std::move(engine)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] creating xds client", this);
  }
  GPR_ASSERT(bootstrap_ != nullptr);
  if (bootstrap_->node() != nullptr) {
    gpr_log(GPR_INFO, "[xds_client %p] xDS node ID: %s", this,
            bootstrap_->node()->id().c_str());
  }
}

XdsClient::~XdsClient() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] destroying xds client", this);
  }
}

void XdsClient::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] shutting down xds client", this);
  }
  MutexLock lock(&mu_);
  shutting_down_ = true;
  // Clear cache and any remaining watchers that may not have been cancelled.
  authority_state_map_.clear();
  invalid_watchers_.clear();
  // We may still be sending lingering queued load report data, so don't
  // just clear the load reporting map, but we do want to clear the refs
  // we're holding to the ChannelState objects, to make sure that
  // everything shuts down properly.
  for (auto& p : xds_load_report_server_map_) {
    p.second.channel_state.reset(DEBUG_LOCATION, "XdsClient::Orphan()");
  }
}

RefCountedPtr<XdsClient::ChannelState> XdsClient::GetOrCreateChannelStateLocked(
    const XdsBootstrap::XdsServer& server, const char* reason) {
  auto it = xds_server_channel_map_.find(&server);
  if (it != xds_server_channel_map_.end()) {
    return it->second->Ref(DEBUG_LOCATION, reason);
  }
  // Channel not found, so create a new one.
  auto channel_state = MakeRefCounted<ChannelState>(
      WeakRef(DEBUG_LOCATION, "ChannelState"), server);
  xds_server_channel_map_[&server] = channel_state.get();
  return channel_state;
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
              watcher->OnError(status);
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
  const XdsBootstrap::XdsServer* xds_server = nullptr;
  absl::string_view authority_name = resource_name->authority;
  if (absl::ConsumePrefix(&authority_name, "xdstp:")) {
    auto* authority = bootstrap_->LookupAuthority(std::string(authority_name));
    if (authority == nullptr) {
      fail(absl::UnavailableError(
          absl::StrCat("authority \"", authority_name,
                       "\" not present in bootstrap config")));
      return;
    }
    xds_server = authority->server();
  }
  if (xds_server == nullptr) xds_server = &bootstrap_->server();
  // Canonify the xDS server instance, so that we make sure we're using
  // the same instance as will be used in AddClusterDropStats() and
  // AddClusterLocalityStats().  This may yield a different result than
  // the logic above if the same server is listed both in the authority
  // and as the top-level server.
  // TODO(roth): This is really ugly -- need to find a better way to
  // index the xDS server than by address here.
  xds_server = bootstrap_->FindXdsServer(*xds_server);
  MutexLock lock(&mu_);
  MaybeRegisterResourceTypeLocked(type);
  AuthorityState& authority_state =
      authority_state_map_[resource_name->authority];
  ResourceState& resource_state =
      authority_state.resource_map[type][resource_name->key];
  resource_state.watchers[w] = watcher;
  // If we already have a cached value for the resource, notify the new
  // watcher immediately.
  if (resource_state.resource != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] returning cached listener data for %s",
              this, std::string(name).c_str());
    }
    work_serializer_.Run([watcher, value = resource_state.resource]()
                             ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
                               watcher->OnGenericResourceChanged(value);
                             });
  } else if (resource_state.meta.client_status ==
             XdsApi::ResourceMetadata::DOES_NOT_EXIST) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] reporting cached does-not-exist for %s", this,
              std::string(name).c_str());
    }
    work_serializer_.Run([watcher]()
                             ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
                               watcher->OnResourceDoesNotExist();
                             });
  } else if (resource_state.meta.client_status ==
             XdsApi::ResourceMetadata::NACKED) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] reporting cached validation failure for %s: %s",
              this, std::string(name).c_str(),
              resource_state.meta.failed_details.c_str());
    }
    std::string details = resource_state.meta.failed_details;
    const auto* node = bootstrap_->node();
    if (node != nullptr) {
      absl::StrAppend(&details, " (node ID:", bootstrap_->node()->id(), ")");
    }
    work_serializer_.Run(
        [watcher, details = std::move(details)]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
              watcher->OnError(absl::UnavailableError(
                  absl::StrCat("invalid resource: ", details)));
            });
  }
  // If the authority doesn't yet have a channel, set it, creating it if
  // needed.
  if (authority_state.channel_state == nullptr) {
    authority_state.channel_state =
        GetOrCreateChannelStateLocked(*xds_server, "start watch");
  }
  absl::Status channel_status = authority_state.channel_state->status();
  if (!channel_status.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] returning cached channel error for %s: %s", this,
              std::string(name).c_str(), channel_status.ToString().c_str());
    }
    work_serializer_.Run(
        [watcher = std::move(watcher), status = std::move(channel_status)]()
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) mutable {
              watcher->OnError(std::move(status));
            });
  }
  authority_state.channel_state->SubscribeLocked(type, *resource_name);
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
      gpr_log(GPR_INFO,
              "[xds_client %p] unsubscribing from a resource for which we "
              "previously ignored a deletion: type %s name %s",
              this, std::string(type->type_url()).c_str(),
              std::string(name).c_str());
    }
    authority_state.channel_state->UnsubscribeLocked(type, *resource_name,
                                                     delay_unsubscription);
    type_map.erase(resource_it);
    if (type_map.empty()) {
      authority_state.resource_map.erase(type_it);
      if (authority_state.resource_map.empty()) {
        authority_state.channel_state.reset();
      }
    }
  }
}

void XdsClient::MaybeRegisterResourceTypeLocked(
    const XdsResourceType* resource_type) {
  auto it = resource_types_.find(resource_type->type_url());
  if (it != resource_types_.end()) {
    GPR_ASSERT(it->second == resource_type);
    return;
  }
  resource_types_.emplace(resource_type->type_url(), resource_type);
  resource_type->InitUpbSymtab(this, symtab_.ptr());
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
  // authority is prefixed with "old:" to indicate that it's an old-style
  // name.
  if (!xds_federation_enabled_ || !absl::StartsWith(name, "xdstp:")) {
    return XdsResourceName{"old:", {std::string(name), {}}};
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
      absl::StrCat("xdstp:", uri->authority()),
      {std::string(path_parts.second), std::move(query_params)}};
}

std::string XdsClient::ConstructFullXdsResourceName(
    absl::string_view authority, absl::string_view resource_type,
    const XdsResourceKey& key) {
  if (absl::ConsumePrefix(&authority, "xdstp:")) {
    auto uri = URI::Create("xdstp", std::string(authority),
                           absl::StrCat("/", resource_type, "/", key.id),
                           key.query_params, /*fragment=*/"");
    GPR_ASSERT(uri.ok());
    return uri->ToString();
  }
  // Old-style name.
  return key.id;
}

RefCountedPtr<XdsClusterDropStats> XdsClient::AddClusterDropStats(
    const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
    absl::string_view eds_service_name) {
  const auto* server = bootstrap_->FindXdsServer(xds_server);
  if (server == nullptr) return nullptr;
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  RefCountedPtr<XdsClusterDropStats> cluster_drop_stats;
  MutexLock lock(&mu_);
  // We jump through some hoops here to make sure that the const
  // XdsBootstrap::XdsServer& and absl::string_views
  // stored in the XdsClusterDropStats object point to the
  // XdsBootstrap::XdsServer and strings
  // in the load_report_map_ key, so that they have the same lifetime.
  auto server_it =
      xds_load_report_server_map_.emplace(server, LoadReportServer()).first;
  if (server_it->second.channel_state == nullptr) {
    server_it->second.channel_state =
        GetOrCreateChannelStateLocked(*server, "load report map (drop stats)");
  }
  auto load_report_it = server_it->second.load_report_map
                            .emplace(std::move(key), LoadReportState())
                            .first;
  LoadReportState& load_report_state = load_report_it->second;
  if (load_report_state.drop_stats != nullptr) {
    cluster_drop_stats = load_report_state.drop_stats->RefIfNonZero();
  }
  if (cluster_drop_stats == nullptr) {
    if (load_report_state.drop_stats != nullptr) {
      load_report_state.deleted_drop_stats +=
          load_report_state.drop_stats->GetSnapshotAndReset();
    }
    cluster_drop_stats = MakeRefCounted<XdsClusterDropStats>(
        Ref(DEBUG_LOCATION, "DropStats"), *server,
        load_report_it->first.first /*cluster_name*/,
        load_report_it->first.second /*eds_service_name*/);
    load_report_state.drop_stats = cluster_drop_stats.get();
  }
  server_it->second.channel_state->MaybeStartLrsCall();
  return cluster_drop_stats;
}

void XdsClient::RemoveClusterDropStats(
    const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    XdsClusterDropStats* cluster_drop_stats) {
  const auto* server = bootstrap_->FindXdsServer(xds_server);
  if (server == nullptr) return;
  MutexLock lock(&mu_);
  auto server_it = xds_load_report_server_map_.find(server);
  if (server_it == xds_load_report_server_map_.end()) return;
  auto load_report_it = server_it->second.load_report_map.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (load_report_it == server_it->second.load_report_map.end()) return;
  LoadReportState& load_report_state = load_report_it->second;
  if (load_report_state.drop_stats == cluster_drop_stats) {
    // Record final snapshot in deleted_drop_stats, which will be
    // added to the next load report.
    load_report_state.deleted_drop_stats +=
        load_report_state.drop_stats->GetSnapshotAndReset();
    load_report_state.drop_stats = nullptr;
  }
}

RefCountedPtr<XdsClusterLocalityStats> XdsClient::AddClusterLocalityStats(
    const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    RefCountedPtr<XdsLocalityName> locality) {
  const auto* server = bootstrap_->FindXdsServer(xds_server);
  if (server == nullptr) return nullptr;
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  RefCountedPtr<XdsClusterLocalityStats> cluster_locality_stats;
  MutexLock lock(&mu_);
  // We jump through some hoops here to make sure that the const
  // XdsBootstrap::XdsServer& and absl::string_views
  // stored in the XdsClusterDropStats object point to the
  // XdsBootstrap::XdsServer and strings
  // in the load_report_map_ key, so that they have the same lifetime.
  auto server_it =
      xds_load_report_server_map_.emplace(server, LoadReportServer()).first;
  if (server_it->second.channel_state == nullptr) {
    server_it->second.channel_state = GetOrCreateChannelStateLocked(
        *server, "load report map (locality stats)");
  }
  auto load_report_it = server_it->second.load_report_map
                            .emplace(std::move(key), LoadReportState())
                            .first;
  LoadReportState& load_report_state = load_report_it->second;
  LoadReportState::LocalityState& locality_state =
      load_report_state.locality_stats[locality];
  if (locality_state.locality_stats != nullptr) {
    cluster_locality_stats = locality_state.locality_stats->RefIfNonZero();
  }
  if (cluster_locality_stats == nullptr) {
    if (locality_state.locality_stats != nullptr) {
      locality_state.deleted_locality_stats +=
          locality_state.locality_stats->GetSnapshotAndReset();
    }
    cluster_locality_stats = MakeRefCounted<XdsClusterLocalityStats>(
        Ref(DEBUG_LOCATION, "LocalityStats"), *server,
        load_report_it->first.first /*cluster_name*/,
        load_report_it->first.second /*eds_service_name*/, std::move(locality));
    locality_state.locality_stats = cluster_locality_stats.get();
  }
  server_it->second.channel_state->MaybeStartLrsCall();
  return cluster_locality_stats;
}

void XdsClient::RemoveClusterLocalityStats(
    const XdsBootstrap::XdsServer& xds_server, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    const RefCountedPtr<XdsLocalityName>& locality,
    XdsClusterLocalityStats* cluster_locality_stats) {
  const auto* server = bootstrap_->FindXdsServer(xds_server);
  if (server == nullptr) return;
  MutexLock lock(&mu_);
  auto server_it = xds_load_report_server_map_.find(server);
  if (server_it == xds_load_report_server_map_.end()) return;
  auto load_report_it = server_it->second.load_report_map.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (load_report_it == server_it->second.load_report_map.end()) return;
  LoadReportState& load_report_state = load_report_it->second;
  auto locality_it = load_report_state.locality_stats.find(locality);
  if (locality_it == load_report_state.locality_stats.end()) return;
  LoadReportState::LocalityState& locality_state = locality_it->second;
  if (locality_state.locality_stats == cluster_locality_stats) {
    // Record final snapshot in deleted_locality_stats, which will be
    // added to the next load report.
    locality_state.deleted_locality_stats +=
        locality_state.locality_stats->GetSnapshotAndReset();
    locality_state.locality_stats = nullptr;
  }
}

void XdsClient::ResetBackoff() {
  MutexLock lock(&mu_);
  for (auto& p : xds_server_channel_map_) {
    p.second->ResetBackoff();
  }
}

void XdsClient::NotifyWatchersOnErrorLocked(
    const std::map<ResourceWatcherInterface*,
                   RefCountedPtr<ResourceWatcherInterface>>& watchers,
    absl::Status status) {
  const auto* node = bootstrap_->node();
  if (node != nullptr) {
    status = absl::Status(
        status.code(),
        absl::StrCat(status.message(), " (node ID:", node->id(), ")"));
  }
  work_serializer_.Run([watchers, status = std::move(status)]()
                           ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
                             for (const auto& p : watchers) {
                               p.first->OnError(status);
                             }
                           });
}

void XdsClient::NotifyWatchersOnResourceDoesNotExist(
    const std::map<ResourceWatcherInterface*,
                   RefCountedPtr<ResourceWatcherInterface>>& watchers) {
  work_serializer_.Run([watchers]()
                           ABSL_EXCLUSIVE_LOCKS_REQUIRED(&work_serializer_) {
                             for (const auto& p : watchers) {
                               p.first->OnResourceDoesNotExist();
                             }
                           });
}

XdsApi::ClusterLoadReportMap XdsClient::BuildLoadReportSnapshotLocked(
    const XdsBootstrap::XdsServer& xds_server, bool send_all_clusters,
    const std::set<std::string>& clusters) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] start building load report", this);
  }
  XdsApi::ClusterLoadReportMap snapshot_map;
  auto server_it = xds_load_report_server_map_.find(&xds_server);
  if (server_it == xds_load_report_server_map_.end()) return snapshot_map;
  auto& load_report_map = server_it->second.load_report_map;
  for (auto load_report_it = load_report_map.begin();
       load_report_it != load_report_map.end();) {
    // Cluster key is cluster and EDS service name.
    const auto& cluster_key = load_report_it->first;
    LoadReportState& load_report = load_report_it->second;
    // If the CDS response for a cluster indicates to use LRS but the
    // LRS server does not say that it wants reports for this cluster,
    // then we'll have stats objects here whose data we're not going to
    // include in the load report.  However, we still need to clear out
    // the data from the stats objects, so that if the LRS server starts
    // asking for the data in the future, we don't incorrectly include
    // data from previous reporting intervals in that future report.
    const bool record_stats =
        send_all_clusters || clusters.find(cluster_key.first) != clusters.end();
    XdsApi::ClusterLoadReport snapshot;
    // Aggregate drop stats.
    snapshot.dropped_requests = std::move(load_report.deleted_drop_stats);
    if (load_report.drop_stats != nullptr) {
      snapshot.dropped_requests +=
          load_report.drop_stats->GetSnapshotAndReset();
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] cluster=%s eds_service_name=%s drop_stats=%p",
                this, cluster_key.first.c_str(), cluster_key.second.c_str(),
                load_report.drop_stats);
      }
    }
    // Aggregate locality stats.
    for (auto it = load_report.locality_stats.begin();
         it != load_report.locality_stats.end();) {
      const RefCountedPtr<XdsLocalityName>& locality_name = it->first;
      auto& locality_state = it->second;
      XdsClusterLocalityStats::Snapshot& locality_snapshot =
          snapshot.locality_stats[locality_name];
      locality_snapshot = std::move(locality_state.deleted_locality_stats);
      if (locality_state.locality_stats != nullptr) {
        locality_snapshot +=
            locality_state.locality_stats->GetSnapshotAndReset();
        if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
          gpr_log(GPR_INFO,
                  "[xds_client %p] cluster=%s eds_service_name=%s "
                  "locality=%s locality_stats=%p",
                  this, cluster_key.first.c_str(), cluster_key.second.c_str(),
                  locality_name->AsHumanReadableString().c_str(),
                  locality_state.locality_stats);
        }
      }
      // If the only thing left in this entry was final snapshots from
      // deleted locality stats objects, remove the entry.
      if (locality_state.locality_stats == nullptr) {
        it = load_report.locality_stats.erase(it);
      } else {
        ++it;
      }
    }
    // Compute load report interval.
    const Timestamp now = Timestamp::Now();
    snapshot.load_report_interval = now - load_report.last_report_time;
    load_report.last_report_time = now;
    // Record snapshot.
    if (record_stats) {
      snapshot_map[cluster_key] = std::move(snapshot);
    }
    // If the only thing left in this entry was final snapshots from
    // deleted stats objects, remove the entry.
    if (load_report.locality_stats.empty() &&
        load_report.drop_stats == nullptr) {
      load_report_it = load_report_map.erase(load_report_it);
    } else {
      ++load_report_it;
    }
  }
  return snapshot_map;
}

std::string XdsClient::DumpClientConfigBinary() {
  MutexLock lock(&mu_);
  XdsApi::ResourceTypeMetadataMap resource_type_metadata_map;
  for (const auto& a : authority_state_map_) {  // authority
    const std::string& authority = a.first;
    for (const auto& t : a.second.resource_map) {  // type
      const XdsResourceType* type = t.first;
      auto& resource_metadata_map =
          resource_type_metadata_map[type->type_url()];
      for (const auto& r : t.second) {  // resource id
        const XdsResourceKey& resource_key = r.first;
        const ResourceState& resource_state = r.second;
        resource_metadata_map[ConstructFullXdsResourceName(
            authority, type->type_url(), resource_key)] = &resource_state.meta;
      }
    }
  }
  // Assemble config dump messages
  return api_.AssembleClientConfig(resource_type_metadata_map);
}

}  // namespace grpc_core
