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

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/xds/xds_api.h"
#include "src/core/ext/xds/xds_bootstrap.h"
#include "src/core/ext/xds/xds_channel_args.h"
#include "src/core/ext/xds/xds_client.h"
#include "src/core/ext/xds/xds_client_stats.h"
#include "src/core/ext/xds/xds_http_filters.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/static_metadata.h"

#define GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_XDS_RECONNECT_JITTER 0.2
#define GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS 1000

namespace grpc_core {

TraceFlag grpc_xds_client_trace(false, "xds_client");
TraceFlag grpc_xds_client_refcount_trace(false, "xds_client_refcount");

namespace {

Mutex* g_mu = nullptr;
const grpc_channel_args* g_channel_args ABSL_GUARDED_BY(*g_mu) = nullptr;
XdsClient* g_xds_client ABSL_GUARDED_BY(*g_mu) = nullptr;
char* g_fallback_bootstrap_config ABSL_GUARDED_BY(*g_mu) = nullptr;

}  // namespace

//
// Internal class declarations
//

// An xds call wrapper that can restart a call upon failure. Holds a ref to
// the xds channel. The template parameter is the kind of wrapped xds call.
template <typename T>
class XdsClient::ChannelState::RetryableCall
    : public InternallyRefCounted<RetryableCall<T>> {
 public:
  explicit RetryableCall(RefCountedPtr<ChannelState> chand);

  void Orphan() override;

  void OnCallFinishedLocked();

  T* calld() const { return calld_.get(); }
  ChannelState* chand() const { return chand_.get(); }

  bool IsCurrentCallOnChannel() const;

 private:
  void StartNewCallLocked();
  void StartRetryTimerLocked();
  static void OnRetryTimer(void* arg, grpc_error_handle error);
  void OnRetryTimerLocked(grpc_error_handle error);

  // The wrapped xds call that talks to the xds server. It's instantiated
  // every time we start a new call. It's null during call retry backoff.
  OrphanablePtr<T> calld_;
  // The owning xds channel.
  RefCountedPtr<ChannelState> chand_;

  // Retry state.
  BackOff backoff_;
  grpc_timer retry_timer_;
  grpc_closure on_retry_timer_;
  bool retry_timer_callback_pending_ = false;

  bool shutting_down_ = false;
};

// Contains an ADS call to the xds server.
class XdsClient::ChannelState::AdsCallState
    : public InternallyRefCounted<AdsCallState> {
 public:
  // The ctor and dtor should not be used directly.
  explicit AdsCallState(RefCountedPtr<RetryableCall<AdsCallState>> parent);
  ~AdsCallState() override;

  void Orphan() override;

  RetryableCall<AdsCallState>* parent() const { return parent_.get(); }
  ChannelState* chand() const { return parent_->chand(); }
  XdsClient* xds_client() const { return chand()->xds_client(); }
  bool seen_response() const { return seen_response_; }

  void SubscribeLocked(const std::string& type_url, const std::string& name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void UnsubscribeLocked(const std::string& type_url, const std::string& name,
                         bool delay_unsubscription)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  bool HasSubscribedResources() const;

 private:
  class ResourceState : public InternallyRefCounted<ResourceState> {
   public:
    ResourceState(const std::string& type_url, const std::string& name,
                  bool sent_initial_request)
        : type_url_(type_url),
          name_(name),
          sent_initial_request_(sent_initial_request) {
      GRPC_CLOSURE_INIT(&timer_callback_, OnTimer, this,
                        grpc_schedule_on_exec_ctx);
    }

    void Orphan() override {
      Finish();
      Unref(DEBUG_LOCATION, "Orphan");
    }

    void Start(RefCountedPtr<AdsCallState> ads_calld) {
      if (sent_initial_request_) return;
      sent_initial_request_ = true;
      ads_calld_ = std::move(ads_calld);
      Ref(DEBUG_LOCATION, "timer").release();
      timer_pending_ = true;
      grpc_timer_init(
          &timer_,
          ExecCtx::Get()->Now() + ads_calld_->xds_client()->request_timeout_,
          &timer_callback_);
    }

    void Finish() {
      if (timer_pending_) {
        grpc_timer_cancel(&timer_);
        timer_pending_ = false;
      }
    }

   private:
    static void OnTimer(void* arg, grpc_error_handle error) {
      ResourceState* self = static_cast<ResourceState*>(arg);
      {
        MutexLock lock(&self->ads_calld_->xds_client()->mu_);
        self->OnTimerLocked(GRPC_ERROR_REF(error));
      }
      self->ads_calld_.reset();
      self->Unref(DEBUG_LOCATION, "timer");
    }

    void OnTimerLocked(grpc_error_handle error)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
      if (error == GRPC_ERROR_NONE && timer_pending_) {
        timer_pending_ = false;
        grpc_error_handle watcher_error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
            absl::StrFormat(
                "timeout obtaining resource {type=%s name=%s} from xds server",
                type_url_, name_)
                .c_str());
        watcher_error = grpc_error_set_int(
            watcher_error, GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_UNAVAILABLE);
        if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
          gpr_log(GPR_INFO, "[xds_client %p] %s", ads_calld_->xds_client(),
                  grpc_error_std_string(watcher_error).c_str());
        }
        if (type_url_ == XdsApi::kLdsTypeUrl) {
          ListenerState& state = ads_calld_->xds_client()->listener_map_[name_];
          state.meta.client_status = XdsApi::ResourceMetadata::DOES_NOT_EXIST;
          for (const auto& p : state.watchers) {
            p.first->OnError(GRPC_ERROR_REF(watcher_error));
          }
        } else if (type_url_ == XdsApi::kRdsTypeUrl) {
          RouteConfigState& state =
              ads_calld_->xds_client()->route_config_map_[name_];
          state.meta.client_status = XdsApi::ResourceMetadata::DOES_NOT_EXIST;
          for (const auto& p : state.watchers) {
            p.first->OnError(GRPC_ERROR_REF(watcher_error));
          }
        } else if (type_url_ == XdsApi::kCdsTypeUrl) {
          ClusterState& state = ads_calld_->xds_client()->cluster_map_[name_];
          state.meta.client_status = XdsApi::ResourceMetadata::DOES_NOT_EXIST;
          for (const auto& p : state.watchers) {
            p.first->OnError(GRPC_ERROR_REF(watcher_error));
          }
        } else if (type_url_ == XdsApi::kEdsTypeUrl) {
          EndpointState& state = ads_calld_->xds_client()->endpoint_map_[name_];
          state.meta.client_status = XdsApi::ResourceMetadata::DOES_NOT_EXIST;
          for (const auto& p : state.watchers) {
            p.first->OnError(GRPC_ERROR_REF(watcher_error));
          }
        } else {
          GPR_UNREACHABLE_CODE(return );
        }
        GRPC_ERROR_UNREF(watcher_error);
      }
      GRPC_ERROR_UNREF(error);
    }

    const std::string type_url_;
    const std::string name_;

    RefCountedPtr<AdsCallState> ads_calld_;
    bool sent_initial_request_;
    bool timer_pending_ = false;
    grpc_timer timer_;
    grpc_closure timer_callback_;
  };

  struct ResourceTypeState {
    ~ResourceTypeState() { GRPC_ERROR_UNREF(error); }

    // Nonce and error for this resource type.
    std::string nonce;
    grpc_error_handle error = GRPC_ERROR_NONE;

    // Subscribed resources of this type.
    std::map<std::string /* name */, OrphanablePtr<ResourceState>>
        subscribed_resources;
  };

  void SendMessageLocked(const std::string& type_url)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  void AcceptLdsUpdateLocked(std::string version, grpc_millis update_time,
                             XdsApi::LdsUpdateMap lds_update_map)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void AcceptRdsUpdateLocked(std::string version, grpc_millis update_time,
                             XdsApi::RdsUpdateMap rds_update_map)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void AcceptCdsUpdateLocked(std::string version, grpc_millis update_time,
                             XdsApi::CdsUpdateMap cds_update_map)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  void AcceptEdsUpdateLocked(std::string version, grpc_millis update_time,
                             XdsApi::EdsUpdateMap eds_update_map)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  static void OnRequestSent(void* arg, grpc_error_handle error);
  void OnRequestSentLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  static void OnResponseReceived(void* arg, grpc_error_handle error);
  bool OnResponseReceivedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  static void OnStatusReceived(void* arg, grpc_error_handle error);
  void OnStatusReceivedLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  bool IsCurrentCallOnChannel() const;

  std::set<absl::string_view> ResourceNamesForRequest(
      const std::string& type_url);

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<AdsCallState>> parent_;

  bool sent_initial_message_ = false;
  bool seen_response_ = false;

  // Always non-NULL.
  grpc_call* call_;

  // recv_initial_metadata
  grpc_metadata_array initial_metadata_recv_;

  // send_message
  grpc_byte_buffer* send_message_payload_ = nullptr;
  grpc_closure on_request_sent_;

  // recv_message
  grpc_byte_buffer* recv_message_payload_ = nullptr;
  grpc_closure on_response_received_;

  // recv_trailing_metadata
  grpc_metadata_array trailing_metadata_recv_;
  grpc_status_code status_code_;
  grpc_slice status_details_;
  grpc_closure on_status_received_;

  // Resource types for which requests need to be sent.
  std::set<std::string /*type_url*/> buffered_requests_;

  // State for each resource type.
  std::map<std::string /*type_url*/, ResourceTypeState> state_map_;
};

// Contains an LRS call to the xds server.
class XdsClient::ChannelState::LrsCallState
    : public InternallyRefCounted<LrsCallState> {
 public:
  // The ctor and dtor should not be used directly.
  explicit LrsCallState(RefCountedPtr<RetryableCall<LrsCallState>> parent);
  ~LrsCallState() override;

  void Orphan() override;

  void MaybeStartReportingLocked();

  RetryableCall<LrsCallState>* parent() { return parent_.get(); }
  ChannelState* chand() const { return parent_->chand(); }
  XdsClient* xds_client() const { return chand()->xds_client(); }
  bool seen_response() const { return seen_response_; }

 private:
  // Reports client-side load stats according to a fixed interval.
  class Reporter : public InternallyRefCounted<Reporter> {
   public:
    Reporter(RefCountedPtr<LrsCallState> parent, grpc_millis report_interval)
        : parent_(std::move(parent)), report_interval_(report_interval) {
      GRPC_CLOSURE_INIT(&on_next_report_timer_, OnNextReportTimer, this,
                        grpc_schedule_on_exec_ctx);
      GRPC_CLOSURE_INIT(&on_report_done_, OnReportDone, this,
                        grpc_schedule_on_exec_ctx);
      ScheduleNextReportLocked();
    }

    void Orphan() override;

   private:
    void ScheduleNextReportLocked()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    static void OnNextReportTimer(void* arg, grpc_error_handle error);
    bool OnNextReportTimerLocked(grpc_error_handle error)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    bool SendReportLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
    static void OnReportDone(void* arg, grpc_error_handle error);
    bool OnReportDoneLocked(grpc_error_handle error)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

    bool IsCurrentReporterOnCall() const {
      return this == parent_->reporter_.get();
    }
    XdsClient* xds_client() const { return parent_->xds_client(); }

    // The owning LRS call.
    RefCountedPtr<LrsCallState> parent_;

    // The load reporting state.
    const grpc_millis report_interval_;
    bool last_report_counters_were_zero_ = false;
    bool next_report_timer_callback_pending_ = false;
    grpc_timer next_report_timer_;
    grpc_closure on_next_report_timer_;
    grpc_closure on_report_done_;
  };

  static void OnInitialRequestSent(void* arg, grpc_error_handle error);
  void OnInitialRequestSentLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  static void OnResponseReceived(void* arg, grpc_error_handle error);
  bool OnResponseReceivedLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);
  static void OnStatusReceived(void* arg, grpc_error_handle error);
  void OnStatusReceivedLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_);

  bool IsCurrentCallOnChannel() const;

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<LrsCallState>> parent_;
  bool seen_response_ = false;

  // Always non-NULL.
  grpc_call* call_;

  // recv_initial_metadata
  grpc_metadata_array initial_metadata_recv_;

  // send_message
  grpc_byte_buffer* send_message_payload_ = nullptr;
  grpc_closure on_initial_request_sent_;

  // recv_message
  grpc_byte_buffer* recv_message_payload_ = nullptr;
  grpc_closure on_response_received_;

  // recv_trailing_metadata
  grpc_metadata_array trailing_metadata_recv_;
  grpc_status_code status_code_;
  grpc_slice status_details_;
  grpc_closure on_status_received_;

  // Load reporting state.
  bool send_all_clusters_ = false;
  std::set<std::string> cluster_names_;  // Asked for by the LRS server.
  grpc_millis load_reporting_interval_ = 0;
  OrphanablePtr<Reporter> reporter_;
};

//
// XdsClient::ChannelState::StateWatcher
//

class XdsClient::ChannelState::StateWatcher
    : public AsyncConnectivityStateWatcherInterface {
 public:
  explicit StateWatcher(RefCountedPtr<ChannelState> parent)
      : parent_(std::move(parent)) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    MutexLock lock(&parent_->xds_client_->mu_);
    if (!parent_->shutting_down_ &&
        new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // In TRANSIENT_FAILURE.  Notify all watchers of error.
      gpr_log(GPR_INFO,
              "[xds_client %p] xds channel in state:TRANSIENT_FAILURE "
              "status_message:(%s)",
              parent_->xds_client(), status.ToString().c_str());
      parent_->xds_client_->NotifyOnErrorLocked(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "xds channel in TRANSIENT_FAILURE"));
    }
  }

  RefCountedPtr<ChannelState> parent_;
};

//
// XdsClient::ChannelState
//

namespace {

grpc_channel* CreateXdsChannel(grpc_channel_args* args,
                               const XdsBootstrap::XdsServer& server) {
  RefCountedPtr<grpc_channel_credentials> channel_creds =
      XdsChannelCredsRegistry::MakeChannelCreds(server.channel_creds_type,
                                                server.channel_creds_config);
  return grpc_secure_channel_create(channel_creds.get(),
                                    server.server_uri.c_str(), args, nullptr);
}

}  // namespace

XdsClient::ChannelState::ChannelState(WeakRefCountedPtr<XdsClient> xds_client,
                                      const XdsBootstrap::XdsServer& server)
    : InternallyRefCounted<ChannelState>(
          GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace)
              ? "ChannelState"
              : nullptr),
      xds_client_(std::move(xds_client)),
      server_(server) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] creating channel to %s",
            xds_client_.get(), server.server_uri.c_str());
  }
  channel_ = CreateXdsChannel(xds_client_->args_, server);
  GPR_ASSERT(channel_ != nullptr);
  StartConnectivityWatchLocked();
}

XdsClient::ChannelState::~ChannelState() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] Destroying xds channel %p", xds_client(),
            this);
  }
  grpc_channel_destroy(channel_);
  xds_client_.reset(DEBUG_LOCATION, "ChannelState");
}

void XdsClient::ChannelState::Orphan() {
  shutting_down_ = true;
  CancelConnectivityWatchLocked();
  ads_calld_.reset();
  lrs_calld_.reset();
  Unref(DEBUG_LOCATION, "ChannelState+orphaned");
}

XdsClient::ChannelState::AdsCallState* XdsClient::ChannelState::ads_calld()
    const {
  return ads_calld_->calld();
}

XdsClient::ChannelState::LrsCallState* XdsClient::ChannelState::lrs_calld()
    const {
  return lrs_calld_->calld();
}

bool XdsClient::ChannelState::HasActiveAdsCall() const {
  return ads_calld_ != nullptr && ads_calld_->calld() != nullptr;
}

void XdsClient::ChannelState::MaybeStartLrsCall() {
  if (lrs_calld_ != nullptr) return;
  lrs_calld_.reset(
      new RetryableCall<LrsCallState>(Ref(DEBUG_LOCATION, "ChannelState+lrs")));
}

void XdsClient::ChannelState::StopLrsCall() { lrs_calld_.reset(); }

void XdsClient::ChannelState::StartConnectivityWatchLocked() {
  ClientChannel* client_channel = ClientChannel::GetFromChannel(channel_);
  GPR_ASSERT(client_channel != nullptr);
  watcher_ = new StateWatcher(Ref(DEBUG_LOCATION, "ChannelState+watch"));
  client_channel->AddConnectivityWatcher(
      GRPC_CHANNEL_IDLE,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface>(watcher_));
}

void XdsClient::ChannelState::CancelConnectivityWatchLocked() {
  ClientChannel* client_channel = ClientChannel::GetFromChannel(channel_);
  GPR_ASSERT(client_channel != nullptr);
  client_channel->RemoveConnectivityWatcher(watcher_);
}

void XdsClient::ChannelState::SubscribeLocked(const std::string& type_url,
                                              const std::string& name) {
  if (ads_calld_ == nullptr) {
    // Start the ADS call if this is the first request.
    ads_calld_.reset(new RetryableCall<AdsCallState>(
        Ref(DEBUG_LOCATION, "ChannelState+ads")));
    // Note: AdsCallState's ctor will automatically subscribe to all
    // resources that the XdsClient already has watchers for, so we can
    // return here.
    return;
  }
  // If the ADS call is in backoff state, we don't need to do anything now
  // because when the call is restarted it will resend all necessary requests.
  if (ads_calld() == nullptr) return;
  // Subscribe to this resource if the ADS call is active.
  ads_calld()->SubscribeLocked(type_url, name);
}

void XdsClient::ChannelState::UnsubscribeLocked(const std::string& type_url,
                                                const std::string& name,
                                                bool delay_unsubscription) {
  if (ads_calld_ != nullptr) {
    auto* calld = ads_calld_->calld();
    if (calld != nullptr) {
      calld->UnsubscribeLocked(type_url, name, delay_unsubscription);
      if (!calld->HasSubscribedResources()) ads_calld_.reset();
    }
  }
}

//
// XdsClient::ChannelState::RetryableCall<>
//

template <typename T>
XdsClient::ChannelState::RetryableCall<T>::RetryableCall(
    RefCountedPtr<ChannelState> chand)
    : chand_(std::move(chand)),
      backoff_(
          BackOff::Options()
              .set_initial_backoff(GRPC_XDS_INITIAL_CONNECT_BACKOFF_SECONDS *
                                   1000)
              .set_multiplier(GRPC_XDS_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(GRPC_XDS_RECONNECT_JITTER)
              .set_max_backoff(GRPC_XDS_RECONNECT_MAX_BACKOFF_SECONDS * 1000)) {
  // Closure Initialization
  GRPC_CLOSURE_INIT(&on_retry_timer_, OnRetryTimer, this,
                    grpc_schedule_on_exec_ctx);
  StartNewCallLocked();
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::Orphan() {
  shutting_down_ = true;
  calld_.reset();
  if (retry_timer_callback_pending_) grpc_timer_cancel(&retry_timer_);
  this->Unref(DEBUG_LOCATION, "RetryableCall+orphaned");
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnCallFinishedLocked() {
  const bool seen_response = calld_->seen_response();
  calld_.reset();
  if (seen_response) {
    // If we lost connection to the xds server, reset backoff and restart the
    // call immediately.
    backoff_.Reset();
    StartNewCallLocked();
  } else {
    // If we failed to connect to the xds server, retry later.
    StartRetryTimerLocked();
  }
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::StartNewCallLocked() {
  if (shutting_down_) return;
  GPR_ASSERT(chand_->channel_ != nullptr);
  GPR_ASSERT(calld_ == nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] Start new call from retryable call (chand: %p, "
            "retryable call: %p)",
            chand()->xds_client(), chand(), this);
  }
  calld_ = MakeOrphanable<T>(
      this->Ref(DEBUG_LOCATION, "RetryableCall+start_new_call"));
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::StartRetryTimerLocked() {
  if (shutting_down_) return;
  const grpc_millis next_attempt_time = backoff_.NextAttemptTime();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    grpc_millis timeout = GPR_MAX(next_attempt_time - ExecCtx::Get()->Now(), 0);
    gpr_log(GPR_INFO,
            "[xds_client %p] Failed to connect to xds server (chand: %p) "
            "retry timer will fire in %" PRId64 "ms.",
            chand()->xds_client(), chand(), timeout);
  }
  this->Ref(DEBUG_LOCATION, "RetryableCall+retry_timer_start").release();
  grpc_timer_init(&retry_timer_, next_attempt_time, &on_retry_timer_);
  retry_timer_callback_pending_ = true;
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnRetryTimer(
    void* arg, grpc_error_handle error) {
  RetryableCall* calld = static_cast<RetryableCall*>(arg);
  {
    MutexLock lock(&calld->chand_->xds_client()->mu_);
    calld->OnRetryTimerLocked(GRPC_ERROR_REF(error));
  }
  calld->Unref(DEBUG_LOCATION, "RetryableCall+retry_timer_done");
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnRetryTimerLocked(
    grpc_error_handle error) {
  retry_timer_callback_pending_ = false;
  if (!shutting_down_ && error == GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(
          GPR_INFO,
          "[xds_client %p] Retry timer fires (chand: %p, retryable call: %p)",
          chand()->xds_client(), chand(), this);
    }
    StartNewCallLocked();
  }
  GRPC_ERROR_UNREF(error);
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
  // Init the ADS call. Note that the call will progress every time there's
  // activity in xds_client()->interested_parties_, which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(xds_client() != nullptr);
  // Create a call with the specified method name.
  const auto& method =
      chand()->server_.ShouldUseV3()
          ? GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_DISCOVERY_DOT_V3_DOT_AGGREGATEDDISCOVERYSERVICE_SLASH_STREAMAGGREGATEDRESOURCES
          : GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_DISCOVERY_DOT_V2_DOT_AGGREGATEDDISCOVERYSERVICE_SLASH_STREAMAGGREGATEDRESOURCES;
  call_ = grpc_channel_create_pollset_set_call(
      chand()->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xds_client()->interested_parties_, method, nullptr,
      GRPC_MILLIS_INF_FUTURE, nullptr);
  GPR_ASSERT(call_ != nullptr);
  // Init data associated with the call.
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  // Start the call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] Starting ADS call (chand: %p, calld: %p, "
            "call: %p)",
            xds_client(), chand(), this, call_);
  }
  // Create the ops.
  grpc_call_error call_error;
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  // Op: send initial metadata.
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY |
              GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  op->reserved = nullptr;
  op++;
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), nullptr);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: send request message.
  GRPC_CLOSURE_INIT(&on_request_sent_, OnRequestSent, this,
                    grpc_schedule_on_exec_ctx);
  for (const auto& p : xds_client()->listener_map_) {
    SubscribeLocked(XdsApi::kLdsTypeUrl, std::string(p.first));
  }
  for (const auto& p : xds_client()->route_config_map_) {
    SubscribeLocked(XdsApi::kRdsTypeUrl, std::string(p.first));
  }
  for (const auto& p : xds_client()->cluster_map_) {
    SubscribeLocked(XdsApi::kCdsTypeUrl, std::string(p.first));
  }
  for (const auto& p : xds_client()->endpoint_map_) {
    SubscribeLocked(XdsApi::kEdsTypeUrl, std::string(p.first));
  }
  // Op: recv initial metadata.
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: recv response.
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  Ref(DEBUG_LOCATION, "ADS+OnResponseReceivedLocked").release();
  GRPC_CLOSURE_INIT(&on_response_received_, OnResponseReceived, this,
                    grpc_schedule_on_exec_ctx);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv server status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &status_code_;
  op->data.recv_status_on_client.status_details = &status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // This callback signals the end of the call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  GRPC_CLOSURE_INIT(&on_status_received_, OnStatusReceived, this,
                    grpc_schedule_on_exec_ctx);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

XdsClient::ChannelState::AdsCallState::~AdsCallState() {
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(status_details_);
  GPR_ASSERT(call_ != nullptr);
  grpc_call_unref(call_);
}

void XdsClient::ChannelState::AdsCallState::Orphan() {
  GPR_ASSERT(call_ != nullptr);
  // If we are here because xds_client wants to cancel the call,
  // on_status_received_ will complete the cancellation and clean up. Otherwise,
  // we are here because xds_client has to orphan a failed call, then the
  // following cancellation will be a no-op.
  grpc_call_cancel_internal(call_);
  state_map_.clear();
  // Note that the initial ref is hold by on_status_received_. So the
  // corresponding unref happens in on_status_received_ instead of here.
}

void XdsClient::ChannelState::AdsCallState::SendMessageLocked(
    const std::string& type_url)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(&XdsClient::mu_) {
  // Buffer message sending if an existing message is in flight.
  if (send_message_payload_ != nullptr) {
    buffered_requests_.insert(type_url);
    return;
  }
  auto& state = state_map_[type_url];
  grpc_slice request_payload_slice;
  std::set<absl::string_view> resource_names =
      ResourceNamesForRequest(type_url);
  request_payload_slice = xds_client()->api_.CreateAdsRequest(
      chand()->server_, type_url, resource_names,
      xds_client()->resource_version_map_[type_url], state.nonce,
      GRPC_ERROR_REF(state.error), !sent_initial_message_);
  if (type_url != XdsApi::kLdsTypeUrl && type_url != XdsApi::kRdsTypeUrl &&
      type_url != XdsApi::kCdsTypeUrl && type_url != XdsApi::kEdsTypeUrl) {
    state_map_.erase(type_url);
  }
  sent_initial_message_ = true;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] sending ADS request: type=%s version=%s nonce=%s "
            "error=%s resources=%s",
            xds_client(), type_url.c_str(),
            xds_client()->resource_version_map_[type_url].c_str(),
            state.nonce.c_str(), grpc_error_std_string(state.error).c_str(),
            absl::StrJoin(resource_names, " ").c_str());
  }
  GRPC_ERROR_UNREF(state.error);
  state.error = GRPC_ERROR_NONE;
  // Create message payload.
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Send the message.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = send_message_payload_;
  Ref(DEBUG_LOCATION, "ADS+OnRequestSentLocked").release();
  GRPC_CLOSURE_INIT(&on_request_sent_, OnRequestSent, this,
                    grpc_schedule_on_exec_ctx);
  grpc_call_error call_error =
      grpc_call_start_batch_and_execute(call_, &op, 1, &on_request_sent_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR,
            "[xds_client %p] calld=%p call_error=%d sending ADS message",
            xds_client(), this, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void XdsClient::ChannelState::AdsCallState::SubscribeLocked(
    const std::string& type_url, const std::string& name) {
  auto& state = state_map_[type_url].subscribed_resources[name];
  if (state == nullptr) {
    state = MakeOrphanable<ResourceState>(
        type_url, name, !xds_client()->resource_version_map_[type_url].empty());
    SendMessageLocked(type_url);
  }
}

void XdsClient::ChannelState::AdsCallState::UnsubscribeLocked(
    const std::string& type_url, const std::string& name,
    bool delay_unsubscription) {
  state_map_[type_url].subscribed_resources.erase(name);
  if (!delay_unsubscription) SendMessageLocked(type_url);
}

bool XdsClient::ChannelState::AdsCallState::HasSubscribedResources() const {
  for (const auto& p : state_map_) {
    if (!p.second.subscribed_resources.empty()) return true;
  }
  return false;
}

namespace {

// Build a resource metadata struct for ADS result accepting methods and CSDS.
XdsApi::ResourceMetadata CreateResourceMetadataAcked(
    std::string serialized_proto, std::string version,
    grpc_millis update_time) {
  XdsApi::ResourceMetadata resource_metadata;
  resource_metadata.serialized_proto = std::move(serialized_proto);
  resource_metadata.update_time = update_time;
  resource_metadata.version = std::move(version);
  resource_metadata.client_status = XdsApi::ResourceMetadata::ACKED;
  return resource_metadata;
}

}  // namespace

void XdsClient::ChannelState::AdsCallState::AcceptLdsUpdateLocked(
    std::string version, grpc_millis update_time,
    XdsApi::LdsUpdateMap lds_update_map) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] LDS update received containing %" PRIuPTR
            " resources",
            xds_client(), lds_update_map.size());
  }
  auto& lds_state = state_map_[XdsApi::kLdsTypeUrl];
  std::set<std::string> rds_resource_names_seen;
  for (auto& p : lds_update_map) {
    const std::string& listener_name = p.first;
    XdsApi::LdsUpdate& lds_update = p.second.resource;
    auto& state = lds_state.subscribed_resources[listener_name];
    if (state != nullptr) state->Finish();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] LDS resource %s: %s", xds_client(),
              listener_name.c_str(), lds_update.ToString().c_str());
    }
    // Record the RDS resource names seen.
    if (!lds_update.http_connection_manager.route_config_name.empty()) {
      rds_resource_names_seen.insert(
          lds_update.http_connection_manager.route_config_name);
    }
    // Ignore identical update.
    ListenerState& listener_state = xds_client()->listener_map_[listener_name];
    if (listener_state.update.has_value() &&
        *listener_state.update == lds_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] LDS update for %s identical to current, "
                "ignoring.",
                xds_client(), listener_name.c_str());
      }
      continue;
    }
    // Update the listener state.
    listener_state.update = std::move(lds_update);
    listener_state.meta = CreateResourceMetadataAcked(
        std::move(p.second.serialized_proto), version, update_time);
    // Notify watchers.
    for (const auto& p : listener_state.watchers) {
      p.first->OnListenerChanged(*listener_state.update);
    }
  }
  // For any subscribed resource that is not present in the update,
  // remove it from the cache and notify watchers that it does not exist.
  for (const auto& p : lds_state.subscribed_resources) {
    const std::string& listener_name = p.first;
    if (lds_update_map.find(listener_name) == lds_update_map.end()) {
      ListenerState& listener_state =
          xds_client()->listener_map_[listener_name];
      // If the resource was newly requested but has not yet been received,
      // we don't want to generate an error for the watchers, because this LDS
      // response may be in reaction to an earlier request that did not yet
      // request the new resource, so its absence from the response does not
      // necessarily indicate that the resource does not exist.
      // For that case, we rely on the request timeout instead.
      if (!listener_state.update.has_value()) continue;
      listener_state.update.reset();
      for (const auto& p : listener_state.watchers) {
        p.first->OnResourceDoesNotExist();
      }
    }
  }
  // For any RDS resource that is no longer referred to by any LDS
  // resources, remove it from the cache and notify watchers that it
  // does not exist.
  auto& rds_state = state_map_[XdsApi::kRdsTypeUrl];
  for (const auto& p : rds_state.subscribed_resources) {
    const std::string& rds_resource_name = p.first;
    if (rds_resource_names_seen.find(rds_resource_name) ==
        rds_resource_names_seen.end()) {
      RouteConfigState& route_config_state =
          xds_client()->route_config_map_[rds_resource_name];
      route_config_state.update.reset();
      for (const auto& p : route_config_state.watchers) {
        p.first->OnResourceDoesNotExist();
      }
    }
  }
}

void XdsClient::ChannelState::AdsCallState::AcceptRdsUpdateLocked(
    std::string version, grpc_millis update_time,
    XdsApi::RdsUpdateMap rds_update_map) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] RDS update received containing %" PRIuPTR
            " resources",
            xds_client(), rds_update_map.size());
  }
  auto& rds_state = state_map_[XdsApi::kRdsTypeUrl];
  for (auto& p : rds_update_map) {
    const std::string& route_config_name = p.first;
    XdsApi::RdsUpdate& rds_update = p.second.resource;
    auto& state = rds_state.subscribed_resources[route_config_name];
    if (state != nullptr) state->Finish();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] RDS resource:\n%s", xds_client(),
              rds_update.ToString().c_str());
    }
    RouteConfigState& route_config_state =
        xds_client()->route_config_map_[route_config_name];
    // Ignore identical update.
    if (route_config_state.update.has_value() &&
        *route_config_state.update == rds_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] RDS resource identical to current, ignoring",
                xds_client());
      }
      continue;
    }
    // Update the cache.
    route_config_state.update = std::move(rds_update);
    route_config_state.meta = CreateResourceMetadataAcked(
        std::move(p.second.serialized_proto), version, update_time);
    // Notify all watchers.
    for (const auto& p : route_config_state.watchers) {
      p.first->OnRouteConfigChanged(*route_config_state.update);
    }
  }
}

void XdsClient::ChannelState::AdsCallState::AcceptCdsUpdateLocked(
    std::string version, grpc_millis update_time,
    XdsApi::CdsUpdateMap cds_update_map) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] CDS update received containing %" PRIuPTR
            " resources",
            xds_client(), cds_update_map.size());
  }
  auto& cds_state = state_map_[XdsApi::kCdsTypeUrl];
  std::set<std::string> eds_resource_names_seen;
  for (auto& p : cds_update_map) {
    const char* cluster_name = p.first.c_str();
    XdsApi::CdsUpdate& cds_update = p.second.resource;
    auto& state = cds_state.subscribed_resources[cluster_name];
    if (state != nullptr) state->Finish();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] cluster=%s: %s", xds_client(),
              cluster_name, cds_update.ToString().c_str());
    }
    // Record the EDS resource names seen.
    eds_resource_names_seen.insert(cds_update.eds_service_name.empty()
                                       ? cluster_name
                                       : cds_update.eds_service_name);
    // Ignore identical update.
    ClusterState& cluster_state = xds_client()->cluster_map_[cluster_name];
    if (cluster_state.update.has_value() &&
        *cluster_state.update == cds_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] CDS update identical to current, ignoring.",
                xds_client());
      }
      continue;
    }
    // Update the cluster state.
    cluster_state.update = std::move(cds_update);
    cluster_state.meta = CreateResourceMetadataAcked(
        std::move(p.second.serialized_proto), version, update_time);
    // Notify all watchers.
    for (const auto& p : cluster_state.watchers) {
      p.first->OnClusterChanged(cluster_state.update.value());
    }
  }
  // For any subscribed resource that is not present in the update,
  // remove it from the cache and notify watchers that it does not exist.
  for (const auto& p : cds_state.subscribed_resources) {
    const std::string& cluster_name = p.first;
    if (cds_update_map.find(cluster_name) == cds_update_map.end()) {
      ClusterState& cluster_state = xds_client()->cluster_map_[cluster_name];
      // If the resource was newly requested but has not yet been received,
      // we don't want to generate an error for the watchers, because this CDS
      // response may be in reaction to an earlier request that did not yet
      // request the new resource, so its absence from the response does not
      // necessarily indicate that the resource does not exist.
      // For that case, we rely on the request timeout instead.
      if (!cluster_state.update.has_value()) continue;
      cluster_state.update.reset();
      for (const auto& p : cluster_state.watchers) {
        p.first->OnResourceDoesNotExist();
      }
    }
  }
  // For any EDS resource that is no longer referred to by any CDS
  // resources, remove it from the cache and notify watchers that it
  // does not exist.
  auto& eds_state = state_map_[XdsApi::kEdsTypeUrl];
  for (const auto& p : eds_state.subscribed_resources) {
    const std::string& eds_resource_name = p.first;
    if (eds_resource_names_seen.find(eds_resource_name) ==
        eds_resource_names_seen.end()) {
      EndpointState& endpoint_state =
          xds_client()->endpoint_map_[eds_resource_name];
      endpoint_state.update.reset();
      for (const auto& p : endpoint_state.watchers) {
        p.first->OnResourceDoesNotExist();
      }
    }
  }
}

void XdsClient::ChannelState::AdsCallState::AcceptEdsUpdateLocked(
    std::string version, grpc_millis update_time,
    XdsApi::EdsUpdateMap eds_update_map) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] EDS update received containing %" PRIuPTR
            " resources",
            xds_client(), eds_update_map.size());
  }
  auto& eds_state = state_map_[XdsApi::kEdsTypeUrl];
  for (auto& p : eds_update_map) {
    const char* eds_service_name = p.first.c_str();
    XdsApi::EdsUpdate& eds_update = p.second.resource;
    auto& state = eds_state.subscribed_resources[eds_service_name];
    if (state != nullptr) state->Finish();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] EDS resource %s: %s", xds_client(),
              eds_service_name, eds_update.ToString().c_str());
    }
    EndpointState& endpoint_state =
        xds_client()->endpoint_map_[eds_service_name];
    // Ignore identical update.
    if (endpoint_state.update.has_value() &&
        *endpoint_state.update == eds_update) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] EDS update identical to current, ignoring.",
                xds_client());
      }
      continue;
    }
    // Update the cluster state.
    endpoint_state.update = std::move(eds_update);
    endpoint_state.meta = CreateResourceMetadataAcked(
        std::move(p.second.serialized_proto), version, update_time);
    // Notify all watchers.
    for (const auto& p : endpoint_state.watchers) {
      p.first->OnEndpointChanged(endpoint_state.update.value());
    }
  }
}

void XdsClient::ChannelState::AdsCallState::OnRequestSent(
    void* arg, grpc_error_handle error) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  {
    MutexLock lock(&ads_calld->xds_client()->mu_);
    ads_calld->OnRequestSentLocked(GRPC_ERROR_REF(error));
  }
  ads_calld->Unref(DEBUG_LOCATION, "ADS+OnRequestSentLocked");
}

void XdsClient::ChannelState::AdsCallState::OnRequestSentLocked(
    grpc_error_handle error) {
  if (IsCurrentCallOnChannel() && error == GRPC_ERROR_NONE) {
    // Clean up the sent message.
    grpc_byte_buffer_destroy(send_message_payload_);
    send_message_payload_ = nullptr;
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
  GRPC_ERROR_UNREF(error);
}

void XdsClient::ChannelState::AdsCallState::OnResponseReceived(
    void* arg, grpc_error_handle /* error */) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  bool done;
  {
    MutexLock lock(&ads_calld->xds_client()->mu_);
    done = ads_calld->OnResponseReceivedLocked();
  }
  if (done) ads_calld->Unref(DEBUG_LOCATION, "ADS+OnResponseReceivedLocked");
}

bool XdsClient::ChannelState::AdsCallState::OnResponseReceivedLocked() {
  // Empty payload means the call was cancelled.
  if (!IsCurrentCallOnChannel() || recv_message_payload_ == nullptr) {
    return true;
  }
  // Read the response.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(recv_message_payload_);
  recv_message_payload_ = nullptr;
  // Parse and validate the response.
  XdsApi::AdsParseResult result = xds_client()->api_.ParseAdsResponse(
      chand()->server_, response_slice,
      ResourceNamesForRequest(XdsApi::kLdsTypeUrl),
      ResourceNamesForRequest(XdsApi::kRdsTypeUrl),
      ResourceNamesForRequest(XdsApi::kCdsTypeUrl),
      ResourceNamesForRequest(XdsApi::kEdsTypeUrl));
  grpc_slice_unref_internal(response_slice);
  if (result.type_url.empty()) {
    // Ignore unparsable response.
    gpr_log(GPR_ERROR,
            "[xds_client %p] Error parsing ADS response (%s) -- ignoring",
            xds_client(), grpc_error_std_string(result.parse_error).c_str());
    GRPC_ERROR_UNREF(result.parse_error);
  } else {
    grpc_millis update_time = grpc_core::ExecCtx::Get()->Now();
    // Update nonce.
    auto& state = state_map_[result.type_url];
    state.nonce = std::move(result.nonce);
    // NACK or ACK the response.
    if (result.parse_error != GRPC_ERROR_NONE) {
      xds_client()->UpdateResourceMetadataWithFailedParseResultLocked(
          update_time, result);
      GRPC_ERROR_UNREF(state.error);
      state.error = result.parse_error;
      // NACK unacceptable update.
      gpr_log(GPR_ERROR,
              "[xds_client %p] ADS response invalid for resource type %s "
              "version %s, will NACK: nonce=%s error=%s",
              xds_client(), result.type_url.c_str(), result.version.c_str(),
              state.nonce.c_str(),
              grpc_error_std_string(result.parse_error).c_str());
      SendMessageLocked(result.type_url);
    } else {
      seen_response_ = true;
      // Accept the ADS response according to the type_url.
      if (result.type_url == XdsApi::kLdsTypeUrl) {
        AcceptLdsUpdateLocked(result.version, update_time,
                              std::move(result.lds_update_map));
      } else if (result.type_url == XdsApi::kRdsTypeUrl) {
        AcceptRdsUpdateLocked(result.version, update_time,
                              std::move(result.rds_update_map));
      } else if (result.type_url == XdsApi::kCdsTypeUrl) {
        AcceptCdsUpdateLocked(result.version, update_time,
                              std::move(result.cds_update_map));
      } else if (result.type_url == XdsApi::kEdsTypeUrl) {
        AcceptEdsUpdateLocked(result.version, update_time,
                              std::move(result.eds_update_map));
      }
      xds_client()->resource_version_map_[result.type_url] =
          std::move(result.version);
      // ACK the update.
      SendMessageLocked(result.type_url);
      // Start load reporting if needed.
      auto& lrs_call = chand()->lrs_calld_;
      if (lrs_call != nullptr) {
        LrsCallState* lrs_calld = lrs_call->calld();
        if (lrs_calld != nullptr) lrs_calld->MaybeStartReportingLocked();
      }
    }
  }
  if (xds_client()->shutting_down_) return true;
  // Keep listening for updates.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  GPR_ASSERT(call_ != nullptr);
  // Reuse the "ADS+OnResponseReceivedLocked" ref taken in ctor.
  const grpc_call_error call_error =
      grpc_call_start_batch_and_execute(call_, &op, 1, &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  return false;
}

void XdsClient::ChannelState::AdsCallState::OnStatusReceived(
    void* arg, grpc_error_handle error) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  {
    MutexLock lock(&ads_calld->xds_client()->mu_);
    ads_calld->OnStatusReceivedLocked(GRPC_ERROR_REF(error));
  }
  ads_calld->Unref(DEBUG_LOCATION, "ADS+OnStatusReceivedLocked");
}

void XdsClient::ChannelState::AdsCallState::OnStatusReceivedLocked(
    grpc_error_handle error) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    char* status_details = grpc_slice_to_c_string(status_details_);
    gpr_log(GPR_INFO,
            "[xds_client %p] ADS call status received. Status = %d, details "
            "= '%s', (chand: %p, ads_calld: %p, call: %p), error '%s'",
            xds_client(), status_code_, status_details, chand(), this, call_,
            grpc_error_std_string(error).c_str());
    gpr_free(status_details);
  }
  // Ignore status from a stale call.
  if (IsCurrentCallOnChannel()) {
    // Try to restart the call.
    parent_->OnCallFinishedLocked();
    // Send error to all watchers.
    xds_client()->NotifyOnErrorLocked(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("xds call failed"));
  }
  GRPC_ERROR_UNREF(error);
}

bool XdsClient::ChannelState::AdsCallState::IsCurrentCallOnChannel() const {
  // If the retryable ADS call is null (which only happens when the xds channel
  // is shutting down), all the ADS calls are stale.
  if (chand()->ads_calld_ == nullptr) return false;
  return this == chand()->ads_calld_->calld();
}

std::set<absl::string_view>
XdsClient::ChannelState::AdsCallState::ResourceNamesForRequest(
    const std::string& type_url) {
  std::set<absl::string_view> resource_names;
  auto it = state_map_.find(type_url);
  if (it != state_map_.end()) {
    for (auto& p : it->second.subscribed_resources) {
      resource_names.insert(p.first);
      OrphanablePtr<ResourceState>& state = p.second;
      state->Start(Ref(DEBUG_LOCATION, "ResourceState"));
    }
  }
  return resource_names;
}

//
// XdsClient::ChannelState::LrsCallState::Reporter
//

void XdsClient::ChannelState::LrsCallState::Reporter::Orphan() {
  if (next_report_timer_callback_pending_) {
    grpc_timer_cancel(&next_report_timer_);
  }
}

void XdsClient::ChannelState::LrsCallState::Reporter::
    ScheduleNextReportLocked() {
  const grpc_millis next_report_time = ExecCtx::Get()->Now() + report_interval_;
  grpc_timer_init(&next_report_timer_, next_report_time,
                  &on_next_report_timer_);
  next_report_timer_callback_pending_ = true;
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnNextReportTimer(
    void* arg, grpc_error_handle error) {
  Reporter* self = static_cast<Reporter*>(arg);
  bool done;
  {
    MutexLock lock(&self->xds_client()->mu_);
    done = self->OnNextReportTimerLocked(GRPC_ERROR_REF(error));
  }
  if (done) self->Unref(DEBUG_LOCATION, "Reporter+timer");
}

bool XdsClient::ChannelState::LrsCallState::Reporter::OnNextReportTimerLocked(
    grpc_error_handle error) {
  next_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || !IsCurrentReporterOnCall()) {
    GRPC_ERROR_UNREF(error);
    return true;
  }
  return SendReportLocked();
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
      xds_client()->BuildLoadReportSnapshotLocked(parent_->send_all_clusters_,
                                                  parent_->cluster_names_);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  const bool old_val = last_report_counters_were_zero_;
  last_report_counters_were_zero_ = LoadReportCountersAreZero(snapshot);
  if (old_val && last_report_counters_were_zero_) {
    if (xds_client()->load_report_map_.empty()) {
      parent_->chand()->StopLrsCall();
      return true;
    }
    ScheduleNextReportLocked();
    return false;
  }
  // Create a request that contains the snapshot.
  grpc_slice request_payload_slice =
      xds_client()->api_.CreateLrsRequest(std::move(snapshot));
  parent_->send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Send the report.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_SEND_MESSAGE;
  op.data.send_message.send_message = parent_->send_message_payload_;
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      parent_->call_, &op, 1, &on_report_done_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR,
            "[xds_client %p] calld=%p call_error=%d sending client load report",
            xds_client(), this, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
  return false;
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnReportDone(
    void* arg, grpc_error_handle error) {
  Reporter* self = static_cast<Reporter*>(arg);
  bool done;
  {
    MutexLock lock(&self->xds_client()->mu_);
    done = self->OnReportDoneLocked(GRPC_ERROR_REF(error));
  }
  if (done) self->Unref(DEBUG_LOCATION, "Reporter+report_done");
}

bool XdsClient::ChannelState::LrsCallState::Reporter::OnReportDoneLocked(
    grpc_error_handle error) {
  grpc_byte_buffer_destroy(parent_->send_message_payload_);
  parent_->send_message_payload_ = nullptr;
  // If there are no more registered stats to report, cancel the call.
  if (xds_client()->load_report_map_.empty()) {
    parent_->chand()->StopLrsCall();
    GRPC_ERROR_UNREF(error);
    return true;
  }
  if (error != GRPC_ERROR_NONE || !IsCurrentReporterOnCall()) {
    GRPC_ERROR_UNREF(error);
    // If this reporter is no longer the current one on the call, the reason
    // might be that it was orphaned for a new one due to config update.
    if (!IsCurrentReporterOnCall()) {
      parent_->MaybeStartReportingLocked();
    }
    return true;
  }
  ScheduleNextReportLocked();
  return false;
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
  const auto& method =
      chand()->server_.ShouldUseV3()
          ? GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_LOAD_STATS_DOT_V3_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS
          : GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_LOAD_STATS_DOT_V2_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS;
  call_ = grpc_channel_create_pollset_set_call(
      chand()->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xds_client()->interested_parties_, method, nullptr,
      GRPC_MILLIS_INF_FUTURE, nullptr);
  GPR_ASSERT(call_ != nullptr);
  // Init the request payload.
  grpc_slice request_payload_slice =
      xds_client()->api_.CreateLrsInitialRequest(chand()->server_);
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Init other data associated with the LRS call.
  grpc_metadata_array_init(&initial_metadata_recv_);
  grpc_metadata_array_init(&trailing_metadata_recv_);
  // Start the call.
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] Starting LRS call (chand: %p, calld: %p, "
            "call: %p)",
            xds_client(), chand(), this, call_);
  }
  // Create the ops.
  grpc_call_error call_error;
  grpc_op ops[3];
  memset(ops, 0, sizeof(ops));
  // Op: send initial metadata.
  grpc_op* op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = GRPC_INITIAL_METADATA_WAIT_FOR_READY |
              GRPC_INITIAL_METADATA_WAIT_FOR_READY_EXPLICITLY_SET;
  op->reserved = nullptr;
  op++;
  // Op: send request message.
  GPR_ASSERT(send_message_payload_ != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = send_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  Ref(DEBUG_LOCATION, "LRS+OnInitialRequestSentLocked").release();
  GRPC_CLOSURE_INIT(&on_initial_request_sent_, OnInitialRequestSent, this,
                    grpc_schedule_on_exec_ctx);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_initial_request_sent_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv initial metadata.
  op = ops;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata =
      &initial_metadata_recv_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // Op: recv response.
  op->op = GRPC_OP_RECV_MESSAGE;
  op->data.recv_message.recv_message = &recv_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  Ref(DEBUG_LOCATION, "LRS+OnResponseReceivedLocked").release();
  GRPC_CLOSURE_INIT(&on_response_received_, OnResponseReceived, this,
                    grpc_schedule_on_exec_ctx);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: recv server status.
  op = ops;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv_;
  op->data.recv_status_on_client.status = &status_code_;
  op->data.recv_status_on_client.status_details = &status_details_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  // This callback signals the end of the call, so it relies on the initial
  // ref instead of a new ref. When it's invoked, it's the initial ref that is
  // unreffed.
  GRPC_CLOSURE_INIT(&on_status_received_, OnStatusReceived, this,
                    grpc_schedule_on_exec_ctx);
  call_error = grpc_call_start_batch_and_execute(
      call_, ops, static_cast<size_t>(op - ops), &on_status_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

XdsClient::ChannelState::LrsCallState::~LrsCallState() {
  grpc_metadata_array_destroy(&initial_metadata_recv_);
  grpc_metadata_array_destroy(&trailing_metadata_recv_);
  grpc_byte_buffer_destroy(send_message_payload_);
  grpc_byte_buffer_destroy(recv_message_payload_);
  grpc_slice_unref_internal(status_details_);
  GPR_ASSERT(call_ != nullptr);
  grpc_call_unref(call_);
}

void XdsClient::ChannelState::LrsCallState::Orphan() {
  reporter_.reset();
  GPR_ASSERT(call_ != nullptr);
  // If we are here because xds_client wants to cancel the call,
  // on_status_received_ will complete the cancellation and clean up. Otherwise,
  // we are here because xds_client has to orphan a failed call, then the
  // following cancellation will be a no-op.
  grpc_call_cancel_internal(call_);
  // Note that the initial ref is hold by on_status_received_. So the
  // corresponding unref happens in on_status_received_ instead of here.
}

void XdsClient::ChannelState::LrsCallState::MaybeStartReportingLocked() {
  // Don't start again if already started.
  if (reporter_ != nullptr) return;
  // Don't start if the previous send_message op (of the initial request or the
  // last report of the previous reporter) hasn't completed.
  if (send_message_payload_ != nullptr) return;
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
  reporter_ = MakeOrphanable<Reporter>(
      Ref(DEBUG_LOCATION, "LRS+load_report+start"), load_reporting_interval_);
}

void XdsClient::ChannelState::LrsCallState::OnInitialRequestSent(
    void* arg, grpc_error_handle /*error*/) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  {
    MutexLock lock(&lrs_calld->xds_client()->mu_);
    lrs_calld->OnInitialRequestSentLocked();
  }
  lrs_calld->Unref(DEBUG_LOCATION, "LRS+OnInitialRequestSentLocked");
}

void XdsClient::ChannelState::LrsCallState::OnInitialRequestSentLocked() {
  // Clear the send_message_payload_.
  grpc_byte_buffer_destroy(send_message_payload_);
  send_message_payload_ = nullptr;
  MaybeStartReportingLocked();
}

void XdsClient::ChannelState::LrsCallState::OnResponseReceived(
    void* arg, grpc_error_handle /*error*/) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  bool done;
  {
    MutexLock lock(&lrs_calld->xds_client()->mu_);
    done = lrs_calld->OnResponseReceivedLocked();
  }
  if (done) lrs_calld->Unref(DEBUG_LOCATION, "LRS+OnResponseReceivedLocked");
}

bool XdsClient::ChannelState::LrsCallState::OnResponseReceivedLocked() {
  // Empty payload means the call was cancelled.
  if (!IsCurrentCallOnChannel() || recv_message_payload_ == nullptr) {
    return true;
  }
  // Read the response.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(recv_message_payload_);
  recv_message_payload_ = nullptr;
  // This anonymous lambda is a hack to avoid the usage of goto.
  [&]() {
    // Parse the response.
    bool send_all_clusters = false;
    std::set<std::string> new_cluster_names;
    grpc_millis new_load_reporting_interval;
    grpc_error_handle parse_error = xds_client()->api_.ParseLrsResponse(
        response_slice, &send_all_clusters, &new_cluster_names,
        &new_load_reporting_interval);
    if (parse_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR,
              "[xds_client %p] LRS response parsing failed. error=%s",
              xds_client(), grpc_error_std_string(parse_error).c_str());
      GRPC_ERROR_UNREF(parse_error);
      return;
    }
    seen_response_ = true;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(
          GPR_INFO,
          "[xds_client %p] LRS response received, %" PRIuPTR
          " cluster names, send_all_clusters=%d, load_report_interval=%" PRId64
          "ms",
          xds_client(), new_cluster_names.size(), send_all_clusters,
          new_load_reporting_interval);
      size_t i = 0;
      for (const auto& name : new_cluster_names) {
        gpr_log(GPR_INFO, "[xds_client %p] cluster_name %" PRIuPTR ": %s",
                xds_client(), i++, name.c_str());
      }
    }
    if (new_load_reporting_interval <
        GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS) {
      new_load_reporting_interval =
          GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS;
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] Increased load_report_interval to minimum "
                "value %dms",
                xds_client(), GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS);
      }
    }
    // Ignore identical update.
    if (send_all_clusters == send_all_clusters_ &&
        cluster_names_ == new_cluster_names &&
        load_reporting_interval_ == new_load_reporting_interval) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] Incoming LRS response identical to current, "
                "ignoring.",
                xds_client());
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
  }();
  grpc_slice_unref_internal(response_slice);
  if (xds_client()->shutting_down_) return true;
  // Keep listening for LRS config updates.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  GPR_ASSERT(call_ != nullptr);
  // Reuse the "OnResponseReceivedLocked" ref taken in ctor.
  const grpc_call_error call_error =
      grpc_call_start_batch_and_execute(call_, &op, 1, &on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  return false;
}

void XdsClient::ChannelState::LrsCallState::OnStatusReceived(
    void* arg, grpc_error_handle error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  {
    MutexLock lock(&lrs_calld->xds_client()->mu_);
    lrs_calld->OnStatusReceivedLocked(GRPC_ERROR_REF(error));
  }
  lrs_calld->Unref(DEBUG_LOCATION, "LRS+OnStatusReceivedLocked");
}

void XdsClient::ChannelState::LrsCallState::OnStatusReceivedLocked(
    grpc_error_handle error) {
  GPR_ASSERT(call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    char* status_details = grpc_slice_to_c_string(status_details_);
    gpr_log(GPR_INFO,
            "[xds_client %p] LRS call status received. Status = %d, details "
            "= '%s', (chand: %p, calld: %p, call: %p), error '%s'",
            xds_client(), status_code_, status_details, chand(), this, call_,
            grpc_error_std_string(error).c_str());
    gpr_free(status_details);
  }
  // Ignore status from a stale call.
  if (IsCurrentCallOnChannel()) {
    GPR_ASSERT(!xds_client()->shutting_down_);
    // Try to restart the call.
    parent_->OnCallFinishedLocked();
  }
  GRPC_ERROR_UNREF(error);
}

bool XdsClient::ChannelState::LrsCallState::IsCurrentCallOnChannel() const {
  // If the retryable LRS call is null (which only happens when the xds channel
  // is shutting down), all the LRS calls are stale.
  if (chand()->lrs_calld_ == nullptr) return false;
  return this == chand()->lrs_calld_->calld();
}

//
// XdsClient
//

namespace {

grpc_millis GetRequestTimeout(const grpc_channel_args* args) {
  return grpc_channel_args_find_integer(
      args, GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS,
      {15000, 0, INT_MAX});
}

grpc_channel_args* ModifyChannelArgs(const grpc_channel_args* args) {
  absl::InlinedVector<grpc_arg, 2> args_to_add = {
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS),
          5 * 60 * GPR_MS_PER_SEC),
      grpc_channel_arg_integer_create(
          const_cast<char*>(GRPC_ARG_CHANNELZ_IS_INTERNAL_CHANNEL), 1),
  };
  return grpc_channel_args_copy_and_add(args, args_to_add.data(),
                                        args_to_add.size());
}

}  // namespace

XdsClient::XdsClient(std::unique_ptr<XdsBootstrap> bootstrap,
                     const grpc_channel_args* args)
    : DualRefCounted<XdsClient>(
          GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_refcount_trace) ? "XdsClient"
                                                                  : nullptr),
      bootstrap_(std::move(bootstrap)),
      args_(ModifyChannelArgs(args)),
      request_timeout_(GetRequestTimeout(args)),
      interested_parties_(grpc_pollset_set_create()),
      certificate_provider_store_(MakeOrphanable<CertificateProviderStore>(
          bootstrap_->certificate_providers())),
      api_(this, &grpc_xds_client_trace, bootstrap_->node()) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] creating xds client", this);
  }
  // Create ChannelState object.
  chand_ = MakeOrphanable<ChannelState>(
      WeakRef(DEBUG_LOCATION, "XdsClient+ChannelState"), bootstrap_->server());
}

XdsClient::~XdsClient() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] destroying xds client", this);
  }
  grpc_channel_args_destroy(args_);
  grpc_pollset_set_destroy(interested_parties_);
}

void XdsClient::AddChannelzLinkage(
    channelz::ChannelNode* parent_channelz_node) {
  MutexLock lock(&mu_);
  channelz::ChannelNode* xds_channelz_node =
      grpc_channel_get_channelz_node(chand_->channel());
  if (xds_channelz_node != nullptr) {
    parent_channelz_node->AddChildChannel(xds_channelz_node->uuid());
  }
}

void XdsClient::RemoveChannelzLinkage(
    channelz::ChannelNode* parent_channelz_node) {
  MutexLock lock(&mu_);
  channelz::ChannelNode* xds_channelz_node =
      grpc_channel_get_channelz_node(chand_->channel());
  if (xds_channelz_node != nullptr) {
    parent_channelz_node->RemoveChildChannel(xds_channelz_node->uuid());
  }
}

void XdsClient::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] shutting down xds client", this);
  }
  {
    MutexLock lock(g_mu);
    if (g_xds_client == this) g_xds_client = nullptr;
  }
  {
    MutexLock lock(&mu_);
    shutting_down_ = true;
    // Orphan ChannelState object.
    chand_.reset();
    // We do not clear cluster_map_ and endpoint_map_ if the xds client was
    // created by the XdsResolver because the maps contain refs for watchers
    // which in turn hold refs to the loadbalancing policies. At this point, it
    // is possible for ADS calls to be in progress. Unreffing the loadbalancing
    // policies before those calls are done would lead to issues such as
    // https://github.com/grpc/grpc/issues/20928.
    if (!listener_map_.empty()) {
      cluster_map_.clear();
      endpoint_map_.clear();
    }
  }
}

void XdsClient::WatchListenerData(
    absl::string_view listener_name,
    std::unique_ptr<ListenerWatcherInterface> watcher) {
  std::string listener_name_str = std::string(listener_name);
  MutexLock lock(&mu_);
  ListenerState& listener_state = listener_map_[listener_name_str];
  ListenerWatcherInterface* w = watcher.get();
  listener_state.watchers[w] = std::move(watcher);
  // If we've already received an LDS update, notify the new watcher
  // immediately.
  if (listener_state.update.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] returning cached listener data for %s",
              this, listener_name_str.c_str());
    }
    w->OnListenerChanged(*listener_state.update);
  }
  chand_->SubscribeLocked(XdsApi::kLdsTypeUrl, listener_name_str);
}

void XdsClient::CancelListenerDataWatch(absl::string_view listener_name,
                                        ListenerWatcherInterface* watcher,
                                        bool delay_unsubscription) {
  MutexLock lock(&mu_);
  if (shutting_down_) return;
  std::string listener_name_str = std::string(listener_name);
  ListenerState& listener_state = listener_map_[listener_name_str];
  auto it = listener_state.watchers.find(watcher);
  if (it != listener_state.watchers.end()) {
    listener_state.watchers.erase(it);
    if (listener_state.watchers.empty()) {
      listener_map_.erase(listener_name_str);
      chand_->UnsubscribeLocked(XdsApi::kLdsTypeUrl, listener_name_str,
                                delay_unsubscription);
    }
  }
}

void XdsClient::WatchRouteConfigData(
    absl::string_view route_config_name,
    std::unique_ptr<RouteConfigWatcherInterface> watcher) {
  std::string route_config_name_str = std::string(route_config_name);
  MutexLock lock(&mu_);
  RouteConfigState& route_config_state =
      route_config_map_[route_config_name_str];
  RouteConfigWatcherInterface* w = watcher.get();
  route_config_state.watchers[w] = std::move(watcher);
  // If we've already received an RDS update, notify the new watcher
  // immediately.
  if (route_config_state.update.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] returning cached route config data for %s", this,
              route_config_name_str.c_str());
    }
    w->OnRouteConfigChanged(*route_config_state.update);
  }
  chand_->SubscribeLocked(XdsApi::kRdsTypeUrl, route_config_name_str);
}

void XdsClient::CancelRouteConfigDataWatch(absl::string_view route_config_name,
                                           RouteConfigWatcherInterface* watcher,
                                           bool delay_unsubscription) {
  MutexLock lock(&mu_);
  if (shutting_down_) return;
  std::string route_config_name_str = std::string(route_config_name);
  RouteConfigState& route_config_state =
      route_config_map_[route_config_name_str];
  auto it = route_config_state.watchers.find(watcher);
  if (it != route_config_state.watchers.end()) {
    route_config_state.watchers.erase(it);
    if (route_config_state.watchers.empty()) {
      route_config_map_.erase(route_config_name_str);
      chand_->UnsubscribeLocked(XdsApi::kRdsTypeUrl, route_config_name_str,
                                delay_unsubscription);
    }
  }
}

void XdsClient::WatchClusterData(
    absl::string_view cluster_name,
    std::unique_ptr<ClusterWatcherInterface> watcher) {
  std::string cluster_name_str = std::string(cluster_name);
  MutexLock lock(&mu_);
  ClusterState& cluster_state = cluster_map_[cluster_name_str];
  ClusterWatcherInterface* w = watcher.get();
  cluster_state.watchers[w] = std::move(watcher);
  // If we've already received a CDS update, notify the new watcher
  // immediately.
  if (cluster_state.update.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] returning cached cluster data for %s",
              this, cluster_name_str.c_str());
    }
    w->OnClusterChanged(cluster_state.update.value());
  }
  chand_->SubscribeLocked(XdsApi::kCdsTypeUrl, cluster_name_str);
}

void XdsClient::CancelClusterDataWatch(absl::string_view cluster_name,
                                       ClusterWatcherInterface* watcher,
                                       bool delay_unsubscription) {
  MutexLock lock(&mu_);
  if (shutting_down_) return;
  std::string cluster_name_str = std::string(cluster_name);
  ClusterState& cluster_state = cluster_map_[cluster_name_str];
  auto it = cluster_state.watchers.find(watcher);
  if (it != cluster_state.watchers.end()) {
    cluster_state.watchers.erase(it);
    if (cluster_state.watchers.empty()) {
      cluster_map_.erase(cluster_name_str);
      chand_->UnsubscribeLocked(XdsApi::kCdsTypeUrl, cluster_name_str,
                                delay_unsubscription);
    }
  }
}

void XdsClient::WatchEndpointData(
    absl::string_view eds_service_name,
    std::unique_ptr<EndpointWatcherInterface> watcher) {
  std::string eds_service_name_str = std::string(eds_service_name);
  MutexLock lock(&mu_);
  EndpointState& endpoint_state = endpoint_map_[eds_service_name_str];
  EndpointWatcherInterface* w = watcher.get();
  endpoint_state.watchers[w] = std::move(watcher);
  // If we've already received an EDS update, notify the new watcher
  // immediately.
  if (endpoint_state.update.has_value()) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p] returning cached endpoint data for %s",
              this, eds_service_name_str.c_str());
    }
    w->OnEndpointChanged(endpoint_state.update.value());
  }
  chand_->SubscribeLocked(XdsApi::kEdsTypeUrl, eds_service_name_str);
}

void XdsClient::CancelEndpointDataWatch(absl::string_view eds_service_name,
                                        EndpointWatcherInterface* watcher,
                                        bool delay_unsubscription) {
  MutexLock lock(&mu_);
  if (shutting_down_) return;
  std::string eds_service_name_str = std::string(eds_service_name);
  EndpointState& endpoint_state = endpoint_map_[eds_service_name_str];
  auto it = endpoint_state.watchers.find(watcher);
  if (it != endpoint_state.watchers.end()) {
    endpoint_state.watchers.erase(it);
    if (endpoint_state.watchers.empty()) {
      endpoint_map_.erase(eds_service_name_str);
      chand_->UnsubscribeLocked(XdsApi::kEdsTypeUrl, eds_service_name_str,
                                delay_unsubscription);
    }
  }
}

RefCountedPtr<XdsClusterDropStats> XdsClient::AddClusterDropStats(
    absl::string_view lrs_server, absl::string_view cluster_name,
    absl::string_view eds_service_name) {
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  MutexLock lock(&mu_);
  // We jump through some hoops here to make sure that the absl::string_views
  // stored in the XdsClusterDropStats object point to the strings
  // in the load_report_map_ key, so that they have the same lifetime.
  auto it = load_report_map_
                .emplace(std::make_pair(std::move(key), LoadReportState()))
                .first;
  LoadReportState& load_report_state = it->second;
  RefCountedPtr<XdsClusterDropStats> cluster_drop_stats;
  if (load_report_state.drop_stats != nullptr) {
    cluster_drop_stats = load_report_state.drop_stats->RefIfNonZero();
  }
  if (cluster_drop_stats == nullptr) {
    if (load_report_state.drop_stats != nullptr) {
      load_report_state.deleted_drop_stats +=
          load_report_state.drop_stats->GetSnapshotAndReset();
    }
    cluster_drop_stats = MakeRefCounted<XdsClusterDropStats>(
        Ref(DEBUG_LOCATION, "DropStats"), lrs_server,
        it->first.first /*cluster_name*/,
        it->first.second /*eds_service_name*/);
    load_report_state.drop_stats = cluster_drop_stats.get();
  }
  chand_->MaybeStartLrsCall();
  return cluster_drop_stats;
}

void XdsClient::RemoveClusterDropStats(
    absl::string_view /*lrs_server*/, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    XdsClusterDropStats* cluster_drop_stats) {
  MutexLock lock(&mu_);
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  auto it = load_report_map_.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (it == load_report_map_.end()) return;
  LoadReportState& load_report_state = it->second;
  if (load_report_state.drop_stats == cluster_drop_stats) {
    // Record final snapshot in deleted_drop_stats, which will be
    // added to the next load report.
    load_report_state.deleted_drop_stats +=
        load_report_state.drop_stats->GetSnapshotAndReset();
    load_report_state.drop_stats = nullptr;
  }
}

RefCountedPtr<XdsClusterLocalityStats> XdsClient::AddClusterLocalityStats(
    absl::string_view lrs_server, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    RefCountedPtr<XdsLocalityName> locality) {
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  MutexLock lock(&mu_);
  // We jump through some hoops here to make sure that the absl::string_views
  // stored in the XdsClusterLocalityStats object point to the strings
  // in the load_report_map_ key, so that they have the same lifetime.
  auto it = load_report_map_
                .emplace(std::make_pair(std::move(key), LoadReportState()))
                .first;
  LoadReportState& load_report_state = it->second;
  LoadReportState::LocalityState& locality_state =
      load_report_state.locality_stats[locality];
  RefCountedPtr<XdsClusterLocalityStats> cluster_locality_stats;
  if (locality_state.locality_stats != nullptr) {
    cluster_locality_stats = locality_state.locality_stats->RefIfNonZero();
  }
  if (cluster_locality_stats == nullptr) {
    if (locality_state.locality_stats != nullptr) {
      locality_state.deleted_locality_stats +=
          locality_state.locality_stats->GetSnapshotAndReset();
    }
    cluster_locality_stats = MakeRefCounted<XdsClusterLocalityStats>(
        Ref(DEBUG_LOCATION, "LocalityStats"), lrs_server,
        it->first.first /*cluster_name*/, it->first.second /*eds_service_name*/,
        std::move(locality));
    locality_state.locality_stats = cluster_locality_stats.get();
  }
  chand_->MaybeStartLrsCall();
  return cluster_locality_stats;
}

void XdsClient::RemoveClusterLocalityStats(
    absl::string_view /*lrs_server*/, absl::string_view cluster_name,
    absl::string_view eds_service_name,
    const RefCountedPtr<XdsLocalityName>& locality,
    XdsClusterLocalityStats* cluster_locality_stats) {
  MutexLock lock(&mu_);
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  auto it = load_report_map_.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (it == load_report_map_.end()) return;
  LoadReportState& load_report_state = it->second;
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
  if (chand_ != nullptr) {
    grpc_channel_reset_connect_backoff(chand_->channel());
  }
}

void XdsClient::NotifyOnErrorLocked(grpc_error_handle error) {
  for (const auto& p : listener_map_) {
    const ListenerState& listener_state = p.second;
    for (const auto& p : listener_state.watchers) {
      p.first->OnError(GRPC_ERROR_REF(error));
    }
  }
  for (const auto& p : route_config_map_) {
    const RouteConfigState& route_config_state = p.second;
    for (const auto& p : route_config_state.watchers) {
      p.first->OnError(GRPC_ERROR_REF(error));
    }
  }
  for (const auto& p : cluster_map_) {
    const ClusterState& cluster_state = p.second;
    for (const auto& p : cluster_state.watchers) {
      p.first->OnError(GRPC_ERROR_REF(error));
    }
  }
  for (const auto& p : endpoint_map_) {
    const EndpointState& endpoint_state = p.second;
    for (const auto& p : endpoint_state.watchers) {
      p.first->OnError(GRPC_ERROR_REF(error));
    }
  }
  GRPC_ERROR_UNREF(error);
}

XdsApi::ClusterLoadReportMap XdsClient::BuildLoadReportSnapshotLocked(
    bool send_all_clusters, const std::set<std::string>& clusters) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] start building load report", this);
  }
  XdsApi::ClusterLoadReportMap snapshot_map;
  for (auto load_report_it = load_report_map_.begin();
       load_report_it != load_report_map_.end();) {
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
    const grpc_millis now = ExecCtx::Get()->Now();
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
      load_report_it = load_report_map_.erase(load_report_it);
    } else {
      ++load_report_it;
    }
  }
  return snapshot_map;
}

void XdsClient::UpdateResourceMetadataWithFailedParseResultLocked(
    grpc_millis update_time, const XdsApi::AdsParseResult& result) {
  // ADS update is rejected and the resource names in the failed update is
  // available.
  std::string details = grpc_error_std_string(result.parse_error);
  for (auto& name : result.resource_names_failed) {
    XdsApi::ResourceMetadata* resource_metadata = nullptr;
    if (result.type_url == XdsApi::kLdsTypeUrl) {
      auto it = listener_map_.find(name);
      if (it != listener_map_.end()) {
        resource_metadata = &it->second.meta;
      }
    } else if (result.type_url == XdsApi::kRdsTypeUrl) {
      auto it = route_config_map_.find(name);
      if (route_config_map_.find(name) != route_config_map_.end()) {
        resource_metadata = &it->second.meta;
      }
    } else if (result.type_url == XdsApi::kCdsTypeUrl) {
      auto it = cluster_map_.find(name);
      if (cluster_map_.find(name) != cluster_map_.end()) {
        resource_metadata = &it->second.meta;
      }
    } else if (result.type_url == XdsApi::kEdsTypeUrl) {
      auto it = endpoint_map_.find(name);
      if (endpoint_map_.find(name) != endpoint_map_.end()) {
        resource_metadata = &it->second.meta;
      }
    }
    if (resource_metadata == nullptr) {
      return;
    }
    resource_metadata->client_status = XdsApi::ResourceMetadata::NACKED;
    resource_metadata->failed_version = result.version;
    resource_metadata->failed_details = details;
    resource_metadata->failed_update_time = update_time;
  }
}

std::string XdsClient::DumpClientConfigBinary() {
  MutexLock lock(&mu_);
  XdsApi::ResourceTypeMetadataMap resource_type_metadata_map;
  // Update per-xds-type version if available, this version corresponding to the
  // last successful ADS update version.
  for (auto& p : resource_version_map_) {
    resource_type_metadata_map[p.first].version = p.second;
  }
  // Collect resource metadata from listeners
  auto& lds_map =
      resource_type_metadata_map[XdsApi::kLdsTypeUrl].resource_metadata_map;
  for (auto& p : listener_map_) {
    lds_map[p.first] = &p.second.meta;
  }
  // Collect resource metadata from route configs
  auto& rds_map =
      resource_type_metadata_map[XdsApi::kRdsTypeUrl].resource_metadata_map;
  for (auto& p : route_config_map_) {
    rds_map[p.first] = &p.second.meta;
  }
  // Collect resource metadata from clusters
  auto& cds_map =
      resource_type_metadata_map[XdsApi::kCdsTypeUrl].resource_metadata_map;
  for (auto& p : cluster_map_) {
    cds_map[p.first] = &p.second.meta;
  }
  // Collect resource metadata from endpoints
  auto& eds_map =
      resource_type_metadata_map[XdsApi::kEdsTypeUrl].resource_metadata_map;
  for (auto& p : endpoint_map_) {
    eds_map[p.first] = &p.second.meta;
  }
  // Assemble config dump messages
  return api_.AssembleClientConfig(resource_type_metadata_map);
}

//
// accessors for global state
//

void XdsClientGlobalInit() {
  g_mu = new Mutex;
  XdsHttpFilterRegistry::Init();
}

// TODO(roth): Find a better way to clear the fallback config that does
// not require using ABSL_NO_THREAD_SAFETY_ANALYSIS.
void XdsClientGlobalShutdown() ABSL_NO_THREAD_SAFETY_ANALYSIS {
  gpr_free(g_fallback_bootstrap_config);
  g_fallback_bootstrap_config = nullptr;
  delete g_mu;
  g_mu = nullptr;
  XdsHttpFilterRegistry::Shutdown();
}

namespace {

std::string GetBootstrapContents(const char* fallback_config,
                                 grpc_error_handle* error) {
  // First, try GRPC_XDS_BOOTSTRAP env var.
  grpc_core::UniquePtr<char> path(gpr_getenv("GRPC_XDS_BOOTSTRAP"));
  if (path != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "Got bootstrap file location from GRPC_XDS_BOOTSTRAP "
              "environment variable: %s",
              path.get());
    }
    grpc_slice contents;
    *error =
        grpc_load_file(path.get(), /*add_null_terminator=*/true, &contents);
    if (*error != GRPC_ERROR_NONE) return "";
    std::string contents_str(StringViewFromSlice(contents));
    grpc_slice_unref_internal(contents);
    return contents_str;
  }
  // Next, try GRPC_XDS_BOOTSTRAP_CONFIG env var.
  grpc_core::UniquePtr<char> env_config(
      gpr_getenv("GRPC_XDS_BOOTSTRAP_CONFIG"));
  if (env_config != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "Got bootstrap contents from GRPC_XDS_BOOTSTRAP_CONFIG "
              "environment variable");
    }
    return env_config.get();
  }
  // Finally, try fallback config.
  if (fallback_config != nullptr) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "Got bootstrap contents from fallback config");
    }
    return fallback_config;
  }
  // No bootstrap config found.
  *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "Environment variables GRPC_XDS_BOOTSTRAP or GRPC_XDS_BOOTSTRAP_CONFIG "
      "not defined");
  return "";
}

}  // namespace

RefCountedPtr<XdsClient> XdsClient::GetOrCreate(const grpc_channel_args* args,
                                                grpc_error_handle* error) {
  RefCountedPtr<XdsClient> xds_client;
  // If getting bootstrap from channel args, create a local XdsClient
  // instance for the channel or server instead of using the global instance.
  const char* bootstrap_config = grpc_channel_args_find_string(
      args, GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_BOOTSTRAP_CONFIG);
  if (bootstrap_config != nullptr) {
    std::unique_ptr<XdsBootstrap> bootstrap =
        XdsBootstrap::Create(bootstrap_config, error);
    if (*error == GRPC_ERROR_NONE) {
      grpc_channel_args* xds_channel_args =
          grpc_channel_args_find_pointer<grpc_channel_args>(
              args,
              GRPC_ARG_TEST_ONLY_DO_NOT_USE_IN_PROD_XDS_CLIENT_CHANNEL_ARGS);
      return MakeRefCounted<XdsClient>(std::move(bootstrap), xds_channel_args);
    }
    return nullptr;
  }
  // Otherwise, use the global instance.
  {
    MutexLock lock(g_mu);
    if (g_xds_client != nullptr) {
      auto xds_client = g_xds_client->RefIfNonZero();
      if (xds_client != nullptr) return xds_client;
    }
    // Find bootstrap contents.
    std::string bootstrap_contents =
        GetBootstrapContents(g_fallback_bootstrap_config, error);
    if (*error != GRPC_ERROR_NONE) return nullptr;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "xDS bootstrap contents: %s",
              bootstrap_contents.c_str());
    }
    // Parse bootstrap.
    std::unique_ptr<XdsBootstrap> bootstrap =
        XdsBootstrap::Create(bootstrap_contents, error);
    if (*error != GRPC_ERROR_NONE) return nullptr;
    // Instantiate XdsClient.
    xds_client =
        MakeRefCounted<XdsClient>(std::move(bootstrap), g_channel_args);
    g_xds_client = xds_client.get();
  }
  return xds_client;
}

namespace internal {

void SetXdsChannelArgsForTest(grpc_channel_args* args) {
  MutexLock lock(g_mu);
  g_channel_args = args;
}

void UnsetGlobalXdsClientForTest() {
  MutexLock lock(g_mu);
  g_xds_client = nullptr;
}

void SetXdsFallbackBootstrapConfig(const char* config) {
  MutexLock lock(g_mu);
  gpr_free(g_fallback_bootstrap_config);
  g_fallback_bootstrap_config = gpr_strdup(config);
}

}  // namespace internal

//
// embedding XdsClient in channel args
//

#define GRPC_ARG_XDS_CLIENT "grpc.internal.xds_client"

namespace {

void* XdsClientArgCopy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Ref(DEBUG_LOCATION, "channel arg").release();
  return p;
}

void XdsClientArgDestroy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Unref(DEBUG_LOCATION, "channel arg");
}

int XdsClientArgCmp(void* p, void* q) { return GPR_ICMP(p, q); }

const grpc_arg_pointer_vtable kXdsClientArgVtable = {
    XdsClientArgCopy, XdsClientArgDestroy, XdsClientArgCmp};

}  // namespace

grpc_arg XdsClient::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(const_cast<char*>(GRPC_ARG_XDS_CLIENT),
                                         const_cast<XdsClient*>(this),
                                         &kXdsClientArgVtable);
}

RefCountedPtr<XdsClient> XdsClient::GetFromChannelArgs(
    const grpc_channel_args& args) {
  XdsClient* xds_client =
      grpc_channel_args_find_pointer<XdsClient>(&args, GRPC_ARG_XDS_CLIENT);
  if (xds_client == nullptr) return nullptr;
  return xds_client->Ref(DEBUG_LOCATION, "GetFromChannelArgs");
}

}  // namespace grpc_core

// The returned bytes may contain NULL(0), so we can't use c-string.
grpc_slice grpc_dump_xds_configs() {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  grpc_core::ExecCtx exec_ctx;
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto xds_client = grpc_core::XdsClient::GetOrCreate(nullptr, &error);
  if (error != GRPC_ERROR_NONE) {
    // If we isn't using xDS, just return an empty string.
    GRPC_ERROR_UNREF(error);
    return grpc_empty_slice();
  }
  return grpc_slice_from_cpp_string(xds_client->DumpClientConfigBinary());
}
