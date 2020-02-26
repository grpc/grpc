/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/xds/xds_api.h"
#include "src/core/ext/filters/client_channel/xds/xds_channel.h"
#include "src/core/ext/filters/client_channel/xds/xds_channel_args.h"
#include "src/core/ext/filters/client_channel/xds/xds_client.h"
#include "src/core/ext/filters/client_channel/xds/xds_client_stats.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/map.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/slice/slice_hash_table.h"
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
  static void OnRetryTimer(void* arg, grpc_error* error);
  static void OnRetryTimerLocked(void* arg, grpc_error* error);

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

  void Subscribe(const std::string& type_url, const std::string& name);
  void Unsubscribe(const std::string& type_url, const std::string& name);

  bool HasSubscribedResources() const;

 private:
  class ResourceState : public InternallyRefCounted<ResourceState> {
   public:
    ResourceState(const std::string& type_url, const std::string& name)
        : type_url_(type_url), name_(name) {
      GRPC_CLOSURE_INIT(&timer_callback_, OnTimer, this,
                        grpc_schedule_on_exec_ctx);
    }

    void Orphan() override {
      Finish();
      Unref();
    }

    void Start(RefCountedPtr<AdsCallState> ads_calld) {
      if (sent_) return;
      sent_ = true;
      ads_calld_ = std::move(ads_calld);
      Ref().release();
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
    static void OnTimer(void* arg, grpc_error* error) {
      ResourceState* self = static_cast<ResourceState*>(arg);
      self->ads_calld_->xds_client()->combiner_->Run(
          GRPC_CLOSURE_INIT(&self->timer_callback_, OnTimerLocked, self,
                            nullptr),
          GRPC_ERROR_REF(error));
    }

    static void OnTimerLocked(void* arg, grpc_error* error) {
      ResourceState* self = static_cast<ResourceState*>(arg);
      if (error == GRPC_ERROR_NONE && self->timer_pending_) {
        self->timer_pending_ = false;
        char* msg;
        gpr_asprintf(
            &msg,
            "timeout obtaining resource {type=%s name=%s} from xds server",
            self->type_url_.c_str(), self->name_.c_str());
        grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(msg);
        if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
          gpr_log(GPR_INFO, "[xds_client %p] %s",
                  self->ads_calld_->xds_client(), grpc_error_string(error));
        }
        if (self->type_url_ == XdsApi::kLdsTypeUrl ||
            self->type_url_ == XdsApi::kRdsTypeUrl) {
          self->ads_calld_->xds_client()->service_config_watcher_->OnError(
              error);
        } else if (self->type_url_ == XdsApi::kCdsTypeUrl) {
          ClusterState& state =
              self->ads_calld_->xds_client()->cluster_map_[self->name_];
          for (const auto& p : state.watchers) {
            p.first->OnError(GRPC_ERROR_REF(error));
          }
          GRPC_ERROR_UNREF(error);
        } else if (self->type_url_ == XdsApi::kEdsTypeUrl) {
          EndpointState& state =
              self->ads_calld_->xds_client()->endpoint_map_[self->name_];
          for (const auto& p : state.watchers) {
            p.first->OnError(GRPC_ERROR_REF(error));
          }
          GRPC_ERROR_UNREF(error);
        } else {
          GPR_UNREACHABLE_CODE(return );
        }
      }
      self->ads_calld_.reset();
      self->Unref();
    }

    const std::string type_url_;
    const std::string name_;

    RefCountedPtr<AdsCallState> ads_calld_;
    bool sent_ = false;
    bool timer_pending_ = false;
    grpc_timer timer_;
    grpc_closure timer_callback_;
  };

  struct ResourceTypeState {
    ~ResourceTypeState() { GRPC_ERROR_UNREF(error); }

    // Version, nonce, and error for this resource type.
    std::string version;
    std::string nonce;
    grpc_error* error = GRPC_ERROR_NONE;

    // Subscribed resources of this type.
    std::map<std::string /* name */, OrphanablePtr<ResourceState>>
        subscribed_resources;
  };

  void SendMessageLocked(const std::string& type_url);

  void AcceptLdsUpdate(XdsApi::LdsUpdate lds_update);
  void AcceptRdsUpdate(XdsApi::RdsUpdate rds_update);
  void AcceptCdsUpdate(XdsApi::CdsUpdateMap cds_update_map);
  void AcceptEdsUpdate(XdsApi::EdsUpdateMap eds_update_map);

  static void OnRequestSent(void* arg, grpc_error* error);
  static void OnRequestSentLocked(void* arg, grpc_error* error);
  static void OnResponseReceived(void* arg, grpc_error* error);
  static void OnResponseReceivedLocked(void* arg, grpc_error* error);
  static void OnStatusReceived(void* arg, grpc_error* error);
  static void OnStatusReceivedLocked(void* arg, grpc_error* error);

  bool IsCurrentCallOnChannel() const;

  std::set<StringView> ClusterNamesForRequest();
  std::set<StringView> EdsServiceNamesForRequest();

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
  bool ShouldSendLoadReports(const StringView& cluster_name) const;

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
      ScheduleNextReportLocked();
    }

    void Orphan() override;

   private:
    void ScheduleNextReportLocked();
    static void OnNextReportTimer(void* arg, grpc_error* error);
    static void OnNextReportTimerLocked(void* arg, grpc_error* error);
    void SendReportLocked();
    static void OnReportDone(void* arg, grpc_error* error);
    static void OnReportDoneLocked(void* arg, grpc_error* error);

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

  static void OnInitialRequestSent(void* arg, grpc_error* error);
  static void OnResponseReceived(void* arg, grpc_error* error);
  static void OnStatusReceived(void* arg, grpc_error* error);
  static void OnInitialRequestSentLocked(void* arg, grpc_error* error);
  static void OnResponseReceivedLocked(void* arg, grpc_error* error);
  static void OnStatusReceivedLocked(void* arg, grpc_error* error);

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
      : AsyncConnectivityStateWatcherInterface(parent->xds_client()->combiner_),
        parent_(std::move(parent)) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state) override {
    if (!parent_->shutting_down_ &&
        new_state == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      // In TRANSIENT_FAILURE.  Notify all watchers of error.
      gpr_log(GPR_INFO,
              "[xds_client %p] xds channel in state TRANSIENT_FAILURE",
              parent_->xds_client());
      parent_->xds_client()->NotifyOnError(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "xds channel in TRANSIENT_FAILURE"));
    }
  }

  RefCountedPtr<ChannelState> parent_;
};

//
// XdsClient::ChannelState
//

namespace {

// Returns the channel args for the xds channel.
grpc_channel_args* BuildXdsChannelArgs(const grpc_channel_args& args) {
  static const char* args_to_remove[] = {
      // LB policy name, since we want to use the default (pick_first) in
      // the LB channel.
      GRPC_ARG_LB_POLICY_NAME,
      // The service config that contains the LB config. We don't want to
      // recursively use xds in the LB channel.
      GRPC_ARG_SERVICE_CONFIG,
      // The channel arg for the server URI, since that will be different for
      // the xds channel than for the parent channel.  The client channel
      // factory will re-add this arg with the right value.
      GRPC_ARG_SERVER_URI,
      // The xds channel should use the authority indicated by the target
      // authority table (see \a ModifyXdsChannelArgs),
      // as opposed to the authority from the parent channel.
      GRPC_ARG_DEFAULT_AUTHORITY,
      // Just as for \a GRPC_ARG_DEFAULT_AUTHORITY, the xds channel should be
      // treated as a stand-alone channel and not inherit this argument from the
      // args of the parent channel.
      GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
      // Don't want to pass down channelz node from parent; the balancer
      // channel will get its own.
      GRPC_ARG_CHANNELZ_CHANNEL_NODE,
      // Keepalive interval.  We are explicitly setting our own value below.
      GRPC_ARG_KEEPALIVE_TIME_MS,
  };
  // Channel args to add.
  InlinedVector<grpc_arg, 3> args_to_add;
  // Keepalive interval.
  args_to_add.emplace_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_KEEPALIVE_TIME_MS), 5000));
  // A channel arg indicating that the target is an xds server.
  // TODO(roth): Once we figure out our fallback and credentials story, decide
  // whether this is actually needed.  Note that it's currently used by the
  // fake security connector as well.
  args_to_add.emplace_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ADDRESS_IS_XDS_SERVER), 1));
  // The parent channel's channelz uuid.
  channelz::ChannelNode* channelz_node = nullptr;
  const grpc_arg* arg =
      grpc_channel_args_find(&args, GRPC_ARG_CHANNELZ_CHANNEL_NODE);
  if (arg != nullptr && arg->type == GRPC_ARG_POINTER &&
      arg->value.pointer.p != nullptr) {
    channelz_node = static_cast<channelz::ChannelNode*>(arg->value.pointer.p);
    args_to_add.emplace_back(
        channelz::MakeParentUuidArg(channelz_node->uuid()));
  }
  // Construct channel args.
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      &args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove), args_to_add.data(),
      args_to_add.size());
  // Make any necessary modifications for security.
  return ModifyXdsChannelArgs(new_args);
}

}  // namespace

XdsClient::ChannelState::ChannelState(RefCountedPtr<XdsClient> xds_client,
                                      grpc_channel* channel)
    : InternallyRefCounted<ChannelState>(&grpc_xds_client_trace),
      xds_client_(std::move(xds_client)),
      channel_(channel) {
  GPR_ASSERT(channel_ != nullptr);
  StartConnectivityWatchLocked();
}

XdsClient::ChannelState::~ChannelState() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] Destroying xds channel %p", xds_client(),
            this);
  }
  grpc_channel_destroy(channel_);
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
  return ads_calld_->calld() != nullptr;
}

void XdsClient::ChannelState::MaybeStartLrsCall() {
  if (lrs_calld_ != nullptr) return;
  lrs_calld_.reset(
      new RetryableCall<LrsCallState>(Ref(DEBUG_LOCATION, "ChannelState+lrs")));
}

void XdsClient::ChannelState::StopLrsCall() { lrs_calld_.reset(); }

void XdsClient::ChannelState::StartConnectivityWatchLocked() {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel_));
  GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
  watcher_ = new StateWatcher(Ref());
  grpc_client_channel_start_connectivity_watch(
      client_channel_elem, GRPC_CHANNEL_IDLE,
      OrphanablePtr<AsyncConnectivityStateWatcherInterface>(watcher_));
}

void XdsClient::ChannelState::CancelConnectivityWatchLocked() {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel_));
  GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
  grpc_client_channel_stop_connectivity_watch(client_channel_elem, watcher_);
}

void XdsClient::ChannelState::Subscribe(const std::string& type_url,
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
  ads_calld()->Subscribe(type_url, name);
}

void XdsClient::ChannelState::Unsubscribe(const std::string& type_url,
                                          const std::string& name) {
  if (ads_calld_ != nullptr) {
    ads_calld_->calld()->Unsubscribe(type_url, name);
    if (!ads_calld_->calld()->HasSubscribedResources()) ads_calld_.reset();
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
  GRPC_CLOSURE_INIT(&on_retry_timer_, OnRetryTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&retry_timer_, next_attempt_time, &on_retry_timer_);
  retry_timer_callback_pending_ = true;
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnRetryTimer(
    void* arg, grpc_error* error) {
  RetryableCall* calld = static_cast<RetryableCall*>(arg);
  calld->chand_->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&calld->on_retry_timer_, OnRetryTimerLocked, calld,
                        nullptr),
      GRPC_ERROR_REF(error));
}

template <typename T>
void XdsClient::ChannelState::RetryableCall<T>::OnRetryTimerLocked(
    void* arg, grpc_error* error) {
  RetryableCall* calld = static_cast<RetryableCall*>(arg);
  calld->retry_timer_callback_pending_ = false;
  if (!calld->shutting_down_ && error == GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(
          GPR_INFO,
          "[xds_client %p] Retry timer fires (chand: %p, retryable call: %p)",
          calld->chand()->xds_client(), calld->chand(), calld);
    }
    calld->StartNewCallLocked();
  }
  calld->Unref(DEBUG_LOCATION, "RetryableCall+retry_timer_done");
}

//
// XdsClient::ChannelState::AdsCallState
//

XdsClient::ChannelState::AdsCallState::AdsCallState(
    RefCountedPtr<RetryableCall<AdsCallState>> parent)
    : InternallyRefCounted<AdsCallState>(&grpc_xds_client_trace),
      parent_(std::move(parent)) {
  // Init the ADS call. Note that the call will progress every time there's
  // activity in xds_client()->interested_parties_, which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(xds_client() != nullptr);
  GPR_ASSERT(!xds_client()->server_name_.empty());
  // Create a call with the specified method name.
  call_ = grpc_channel_create_pollset_set_call(
      chand()->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xds_client()->interested_parties_,
      GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_DISCOVERY_DOT_V2_DOT_AGGREGATEDDISCOVERYSERVICE_SLASH_STREAMAGGREGATEDRESOURCES,
      nullptr, GRPC_MILLIS_INF_FUTURE, nullptr);
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
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 nullptr);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
  // Op: send request message.
  GRPC_CLOSURE_INIT(&on_request_sent_, OnRequestSent, this,
                    grpc_schedule_on_exec_ctx);
  if (xds_client()->service_config_watcher_ != nullptr) {
    Subscribe(XdsApi::kLdsTypeUrl, xds_client()->server_name_);
  }
  for (const auto& p : xds_client()->cluster_map_) {
    Subscribe(XdsApi::kCdsTypeUrl, std::string(p.first));
  }
  for (const auto& p : xds_client()->endpoint_map_) {
    Subscribe(XdsApi::kEdsTypeUrl, std::string(p.first));
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
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 &on_response_received_);
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
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 &on_status_received_);
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
  grpc_call_cancel(call_, nullptr);
  state_map_.clear();
  // Note that the initial ref is hold by on_status_received_. So the
  // corresponding unref happens in on_status_received_ instead of here.
}

void XdsClient::ChannelState::AdsCallState::SendMessageLocked(
    const std::string& type_url) {
  // Buffer message sending if an existing message is in flight.
  if (send_message_payload_ != nullptr) {
    buffered_requests_.insert(type_url);
    return;
  }
  auto& state = state_map_[type_url];
  grpc_error* error = state.error;
  state.error = GRPC_ERROR_NONE;
  grpc_slice request_payload_slice;
  if (type_url == XdsApi::kLdsTypeUrl) {
    request_payload_slice = xds_client()->api_.CreateLdsRequest(
        xds_client()->server_name_, state.version, state.nonce, error,
        !sent_initial_message_);
    state.subscribed_resources[xds_client()->server_name_]->Start(Ref());
  } else if (type_url == XdsApi::kRdsTypeUrl) {
    request_payload_slice = xds_client()->api_.CreateRdsRequest(
        xds_client()->route_config_name_, state.version, state.nonce, error,
        !sent_initial_message_);
    state.subscribed_resources[xds_client()->route_config_name_]->Start(Ref());
  } else if (type_url == XdsApi::kCdsTypeUrl) {
    request_payload_slice = xds_client()->api_.CreateCdsRequest(
        ClusterNamesForRequest(), state.version, state.nonce, error,
        !sent_initial_message_);
  } else if (type_url == XdsApi::kEdsTypeUrl) {
    request_payload_slice = xds_client()->api_.CreateEdsRequest(
        EdsServiceNamesForRequest(), state.version, state.nonce, error,
        !sent_initial_message_);
  } else {
    request_payload_slice = xds_client()->api_.CreateUnsupportedTypeNackRequest(
        type_url, state.nonce, state.error);
    state_map_.erase(type_url);
  }
  sent_initial_message_ = true;
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

void XdsClient::ChannelState::AdsCallState::Subscribe(
    const std::string& type_url, const std::string& name) {
  auto& state = state_map_[type_url].subscribed_resources[name];
  if (state == nullptr) {
    state = MakeOrphanable<ResourceState>(type_url, name);
    SendMessageLocked(type_url);
  }
}

void XdsClient::ChannelState::AdsCallState::Unsubscribe(
    const std::string& type_url, const std::string& name) {
  state_map_[type_url].subscribed_resources.erase(name);
  SendMessageLocked(type_url);
}

bool XdsClient::ChannelState::AdsCallState::HasSubscribedResources() const {
  for (const auto& p : state_map_) {
    if (!p.second.subscribed_resources.empty()) return true;
  }
  return false;
}

void XdsClient::ChannelState::AdsCallState::AcceptLdsUpdate(
    XdsApi::LdsUpdate lds_update) {
  const std::string& cluster_name =
      lds_update.rds_update.has_value()
          ? lds_update.rds_update.value().cluster_name
          : "";
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] LDS update received: "
            "route_config_name=%s, "
            "cluster_name=%s (empty if RDS is needed to obtain it)",
            xds_client(), lds_update.route_config_name.c_str(),
            cluster_name.c_str());
  }
  auto& lds_state = state_map_[XdsApi::kLdsTypeUrl];
  auto& state = lds_state.subscribed_resources[xds_client()->server_name_];
  if (state != nullptr) state->Finish();
  // Ignore identical update.
  if (xds_client()->route_config_name_ == lds_update.route_config_name &&
      xds_client()->cluster_name_ == cluster_name) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] LDS update identical to current, ignoring.",
              xds_client());
    }
    return;
  }
  xds_client()->route_config_name_ = std::move(lds_update.route_config_name);
  if (lds_update.rds_update.has_value()) {
    // If cluster_name was found inlined in LDS response, notify the watcher
    // immediately.
    xds_client()->cluster_name_ =
        std::move(lds_update.rds_update.value().cluster_name);
    RefCountedPtr<ServiceConfig> service_config;
    grpc_error* error = xds_client()->CreateServiceConfig(
        xds_client()->cluster_name_, &service_config);
    if (error == GRPC_ERROR_NONE) {
      xds_client()->service_config_watcher_->OnServiceConfigChanged(
          std::move(service_config));
    } else {
      xds_client()->service_config_watcher_->OnError(error);
    }
  } else {
    // Send RDS request for dynamic resolution.
    Subscribe(XdsApi::kRdsTypeUrl, xds_client()->route_config_name_);
  }
}

void XdsClient::ChannelState::AdsCallState::AcceptRdsUpdate(
    XdsApi::RdsUpdate rds_update) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO,
            "[xds_client %p] RDS update received: "
            "cluster_name=%s",
            xds_client(), rds_update.cluster_name.c_str());
  }
  auto& rds_state = state_map_[XdsApi::kRdsTypeUrl];
  auto& state =
      rds_state.subscribed_resources[xds_client()->route_config_name_];
  if (state != nullptr) state->Finish();
  // Ignore identical update.
  if (xds_client()->cluster_name_ == rds_update.cluster_name) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] RDS update identical to current, ignoring.",
              xds_client());
    }
    return;
  }
  xds_client()->cluster_name_ = std::move(rds_update.cluster_name);
  // Notify the watcher.
  RefCountedPtr<ServiceConfig> service_config;
  grpc_error* error = xds_client()->CreateServiceConfig(
      xds_client()->cluster_name_, &service_config);
  if (error == GRPC_ERROR_NONE) {
    xds_client()->service_config_watcher_->OnServiceConfigChanged(
        std::move(service_config));
  } else {
    xds_client()->service_config_watcher_->OnError(error);
  }
}

void XdsClient::ChannelState::AdsCallState::AcceptCdsUpdate(
    XdsApi::CdsUpdateMap cds_update_map) {
  auto& cds_state = state_map_[XdsApi::kCdsTypeUrl];
  for (auto& p : cds_update_map) {
    const char* cluster_name = p.first.c_str();
    XdsApi::CdsUpdate& cds_update = p.second;
    auto& state = cds_state.subscribed_resources[cluster_name];
    if (state != nullptr) state->Finish();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] CDS update (cluster=%s) received: "
              "eds_service_name=%s, "
              "lrs_load_reporting_server_name=%s",
              xds_client(), cluster_name, cds_update.eds_service_name.c_str(),
              cds_update.lrs_load_reporting_server_name.has_value()
                  ? cds_update.lrs_load_reporting_server_name.value().c_str()
                  : "(N/A)");
    }
    ClusterState& cluster_state = xds_client()->cluster_map_[cluster_name];
    // Ignore identical update.
    if (cluster_state.update.has_value() &&
        cds_update.eds_service_name ==
            cluster_state.update.value().eds_service_name &&
        cds_update.lrs_load_reporting_server_name.value() ==
            cluster_state.update.value()
                .lrs_load_reporting_server_name.value()) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] CDS update identical to current, ignoring.",
                xds_client());
      }
      continue;
    }
    // Update the cluster state.
    cluster_state.update.emplace(std::move(cds_update));
    // Notify all watchers.
    for (const auto& p : cluster_state.watchers) {
      p.first->OnClusterChanged(cluster_state.update.value());
    }
  }
}

void XdsClient::ChannelState::AdsCallState::AcceptEdsUpdate(
    XdsApi::EdsUpdateMap eds_update_map) {
  auto& eds_state = state_map_[XdsApi::kEdsTypeUrl];
  for (auto& p : eds_update_map) {
    const char* eds_service_name = p.first.c_str();
    XdsApi::EdsUpdate& eds_update = p.second;
    auto& state = eds_state.subscribed_resources[eds_service_name];
    if (state != nullptr) state->Finish();
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] EDS response with %" PRIuPTR
              " priorities and %" PRIuPTR
              " drop categories received (drop_all=%d)",
              xds_client(), eds_update.priority_list_update.size(),
              eds_update.drop_config->drop_category_list().size(),
              eds_update.drop_all);
      for (size_t priority = 0;
           priority < eds_update.priority_list_update.size(); ++priority) {
        const auto* locality_map_update = eds_update.priority_list_update.Find(
            static_cast<uint32_t>(priority));
        gpr_log(GPR_INFO,
                "[xds_client %p] Priority %" PRIuPTR " contains %" PRIuPTR
                " localities",
                xds_client(), priority, locality_map_update->size());
        size_t locality_count = 0;
        for (const auto& p : locality_map_update->localities) {
          const auto& locality = p.second;
          gpr_log(GPR_INFO,
                  "[xds_client %p] Priority %" PRIuPTR ", locality %" PRIuPTR
                  " %s has weight %d, contains %" PRIuPTR " server addresses",
                  xds_client(), priority, locality_count,
                  locality.name->AsHumanReadableString(), locality.lb_weight,
                  locality.serverlist.size());
          for (size_t i = 0; i < locality.serverlist.size(); ++i) {
            char* ipport;
            grpc_sockaddr_to_string(&ipport, &locality.serverlist[i].address(),
                                    false);
            gpr_log(GPR_INFO,
                    "[xds_client %p] Priority %" PRIuPTR ", locality %" PRIuPTR
                    " %s, server address %" PRIuPTR ": %s",
                    xds_client(), priority, locality_count,
                    locality.name->AsHumanReadableString(), i, ipport);
            gpr_free(ipport);
          }
          ++locality_count;
        }
      }
      for (size_t i = 0;
           i < eds_update.drop_config->drop_category_list().size(); ++i) {
        const XdsApi::DropConfig::DropCategory& drop_category =
            eds_update.drop_config->drop_category_list()[i];
        gpr_log(GPR_INFO,
                "[xds_client %p] Drop category %s has drop rate %d per million",
                xds_client(), drop_category.name.c_str(),
                drop_category.parts_per_million);
      }
    }
    EndpointState& endpoint_state =
        xds_client()->endpoint_map_[eds_service_name];
    // Ignore identical update.
    const XdsApi::EdsUpdate& prev_update = endpoint_state.update;
    const bool priority_list_changed =
        prev_update.priority_list_update != eds_update.priority_list_update;
    const bool drop_config_changed =
        prev_update.drop_config == nullptr ||
        *prev_update.drop_config != *eds_update.drop_config;
    if (!priority_list_changed && !drop_config_changed) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] EDS update identical to current, ignoring.",
                xds_client());
      }
      continue;
    }
    // Update the cluster state.
    endpoint_state.update = std::move(eds_update);
    // Notify all watchers.
    for (const auto& p : endpoint_state.watchers) {
      p.first->OnEndpointChanged(endpoint_state.update);
    }
  }
}

void XdsClient::ChannelState::AdsCallState::OnRequestSent(void* arg,
                                                          grpc_error* error) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  ads_calld->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&ads_calld->on_request_sent_, OnRequestSentLocked,
                        ads_calld, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::AdsCallState::OnRequestSentLocked(
    void* arg, grpc_error* error) {
  AdsCallState* self = static_cast<AdsCallState*>(arg);
  if (self->IsCurrentCallOnChannel() && error == GRPC_ERROR_NONE) {
    // Clean up the sent message.
    grpc_byte_buffer_destroy(self->send_message_payload_);
    self->send_message_payload_ = nullptr;
    // Continue to send another pending message if any.
    // TODO(roth): The current code to handle buffered messages has the
    // advantage of sending only the most recent list of resource names for
    // each resource type (no matter how many times that resource type has
    // been requested to send while the current message sending is still
    // pending). But its disadvantage is that we send the requests in fixed
    // order of resource types. We need to fix this if we are seeing some
    // resource type(s) starved due to frequent requests of other resource
    // type(s).
    auto it = self->buffered_requests_.begin();
    if (it != self->buffered_requests_.end()) {
      self->SendMessageLocked(*it);
      self->buffered_requests_.erase(it);
    }
  }
  self->Unref(DEBUG_LOCATION, "ADS+OnRequestSentLocked");
}

void XdsClient::ChannelState::AdsCallState::OnResponseReceived(
    void* arg, grpc_error* error) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  ads_calld->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&ads_calld->on_response_received_,
                        OnResponseReceivedLocked, ads_calld, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::AdsCallState::OnResponseReceivedLocked(
    void* arg, grpc_error* /*error*/) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  XdsClient* xds_client = ads_calld->xds_client();
  // Empty payload means the call was cancelled.
  if (!ads_calld->IsCurrentCallOnChannel() ||
      ads_calld->recv_message_payload_ == nullptr) {
    ads_calld->Unref(DEBUG_LOCATION, "ADS+OnResponseReceivedLocked");
    return;
  }
  // Read the response.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, ads_calld->recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(ads_calld->recv_message_payload_);
  ads_calld->recv_message_payload_ = nullptr;
  // TODO(juanlishen): When we convert this to use the xds protocol, the
  // balancer will send us a fallback timeout such that we should go into
  // fallback mode if we have lost contact with the balancer after a certain
  // period of time. We will need to save the timeout value here, and then
  // when the balancer call ends, we will need to start a timer for the
  // specified period of time, and if the timer fires, we go into fallback
  // mode. We will also need to cancel the timer when we receive a serverlist
  // from the balancer.
  // Parse the response.
  XdsApi::LdsUpdate lds_update;
  XdsApi::RdsUpdate rds_update;
  XdsApi::CdsUpdateMap cds_update_map;
  XdsApi::EdsUpdateMap eds_update_map;
  std::string version;
  std::string nonce;
  std::string type_url;
  // Note that ParseAdsResponse() also validates the response.
  grpc_error* parse_error = xds_client->api_.ParseAdsResponse(
      response_slice, xds_client->server_name_, xds_client->route_config_name_,
      ads_calld->EdsServiceNamesForRequest(), &lds_update, &rds_update,
      &cds_update_map, &eds_update_map, &version, &nonce, &type_url);
  grpc_slice_unref_internal(response_slice);
  if (type_url.empty()) {
    // Ignore unparsable response.
    gpr_log(GPR_ERROR, "[xds_client %p] No type_url found. error=%s",
            xds_client, grpc_error_string(parse_error));
    GRPC_ERROR_UNREF(parse_error);
  } else {
    // Update nonce.
    auto& state = ads_calld->state_map_[type_url];
    state.nonce = std::move(nonce);
    // NACK or ACK the response.
    if (parse_error != GRPC_ERROR_NONE) {
      GRPC_ERROR_UNREF(state.error);
      state.error = parse_error;
      // NACK unacceptable update.
      gpr_log(
          GPR_ERROR,
          "[xds_client %p] ADS response can't be accepted, NACKing. error=%s",
          xds_client, grpc_error_string(parse_error));
      ads_calld->SendMessageLocked(type_url);
    } else {
      ads_calld->seen_response_ = true;
      // Accept the ADS response according to the type_url.
      if (type_url == XdsApi::kLdsTypeUrl) {
        ads_calld->AcceptLdsUpdate(std::move(lds_update));
      } else if (type_url == XdsApi::kRdsTypeUrl) {
        ads_calld->AcceptRdsUpdate(std::move(rds_update));
      } else if (type_url == XdsApi::kCdsTypeUrl) {
        ads_calld->AcceptCdsUpdate(std::move(cds_update_map));
      } else if (type_url == XdsApi::kEdsTypeUrl) {
        ads_calld->AcceptEdsUpdate(std::move(eds_update_map));
      }
      state.version = std::move(version);
      // ACK the update.
      ads_calld->SendMessageLocked(type_url);
      // Start load reporting if needed.
      auto& lrs_call = ads_calld->chand()->lrs_calld_;
      if (lrs_call != nullptr) {
        LrsCallState* lrs_calld = lrs_call->calld();
        if (lrs_calld != nullptr) lrs_calld->MaybeStartReportingLocked();
      }
    }
  }
  if (xds_client->shutting_down_) {
    ads_calld->Unref(DEBUG_LOCATION,
                     "ADS+OnResponseReceivedLocked+xds_shutdown");
    return;
  }
  // Keep listening for updates.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &ads_calld->recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  GPR_ASSERT(ads_calld->call_ != nullptr);
  // Reuse the "ADS+OnResponseReceivedLocked" ref taken in ctor.
  GRPC_CLOSURE_INIT(&ads_calld->on_response_received_, OnResponseReceived,
                    ads_calld, grpc_schedule_on_exec_ctx);
  const grpc_call_error call_error = grpc_call_start_batch_and_execute(
      ads_calld->call_, &op, 1, &ads_calld->on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsClient::ChannelState::AdsCallState::OnStatusReceived(
    void* arg, grpc_error* error) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  ads_calld->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&ads_calld->on_status_received_, OnStatusReceivedLocked,
                        ads_calld, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::AdsCallState::OnStatusReceivedLocked(
    void* arg, grpc_error* error) {
  AdsCallState* ads_calld = static_cast<AdsCallState*>(arg);
  ChannelState* chand = ads_calld->chand();
  XdsClient* xds_client = ads_calld->xds_client();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    char* status_details = grpc_slice_to_c_string(ads_calld->status_details_);
    gpr_log(GPR_INFO,
            "[xds_client %p] ADS call status received. Status = %d, details "
            "= '%s', (chand: %p, ads_calld: %p, call: %p), error '%s'",
            xds_client, ads_calld->status_code_, status_details, chand,
            ads_calld, ads_calld->call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  // Ignore status from a stale call.
  if (ads_calld->IsCurrentCallOnChannel()) {
    // Try to restart the call.
    ads_calld->parent_->OnCallFinishedLocked();
    // Send error to all watchers.
    xds_client->NotifyOnError(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("xds call failed"));
  }
  ads_calld->Unref(DEBUG_LOCATION, "ADS+OnStatusReceivedLocked");
}

bool XdsClient::ChannelState::AdsCallState::IsCurrentCallOnChannel() const {
  // If the retryable ADS call is null (which only happens when the xds channel
  // is shutting down), all the ADS calls are stale.
  if (chand()->ads_calld_ == nullptr) return false;
  return this == chand()->ads_calld_->calld();
}

std::set<StringView>
XdsClient::ChannelState::AdsCallState::ClusterNamesForRequest() {
  std::set<StringView> cluster_names;
  for (auto& p : state_map_[XdsApi::kCdsTypeUrl].subscribed_resources) {
    cluster_names.insert(p.first);
    OrphanablePtr<ResourceState>& state = p.second;
    state->Start(Ref());
  }
  return cluster_names;
}

std::set<StringView>
XdsClient::ChannelState::AdsCallState::EdsServiceNamesForRequest() {
  std::set<StringView> eds_names;
  for (auto& p : state_map_[XdsApi::kEdsTypeUrl].subscribed_resources) {
    eds_names.insert(p.first);
    OrphanablePtr<ResourceState>& state = p.second;
    state->Start(Ref());
  }
  return eds_names;
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
  GRPC_CLOSURE_INIT(&on_next_report_timer_, OnNextReportTimer, this,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&next_report_timer_, next_report_time,
                  &on_next_report_timer_);
  next_report_timer_callback_pending_ = true;
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnNextReportTimer(
    void* arg, grpc_error* error) {
  Reporter* self = static_cast<Reporter*>(arg);
  self->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&self->on_next_report_timer_, OnNextReportTimerLocked,
                        self, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnNextReportTimerLocked(
    void* arg, grpc_error* error) {
  Reporter* self = static_cast<Reporter*>(arg);
  self->next_report_timer_callback_pending_ = false;
  if (error != GRPC_ERROR_NONE || !self->IsCurrentReporterOnCall()) {
    self->Unref(DEBUG_LOCATION, "Reporter+timer");
    return;
  }
  self->SendReportLocked();
}

namespace {

bool LoadReportCountersAreZero(const XdsApi::ClusterLoadReportMap& snapshot) {
  for (const auto& p : snapshot) {
    const XdsApi::ClusterLoadReport& cluster_snapshot = p.second;
    for (const auto& q : cluster_snapshot.dropped_requests) {
      if (q.second > 0) return false;
    }
    for (const auto& q : cluster_snapshot.locality_stats) {
      const XdsClusterLocalityStats::Snapshot& locality_snapshot = q.second;
      if (!locality_snapshot.IsZero()) return false;
    }
  }
  return true;
}

}  // namespace

void XdsClient::ChannelState::LrsCallState::Reporter::SendReportLocked() {
  // Construct snapshot from all reported stats.
  XdsApi::ClusterLoadReportMap snapshot =
      xds_client()->BuildLoadReportSnapshot();
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  const bool old_val = last_report_counters_were_zero_;
  last_report_counters_were_zero_ = LoadReportCountersAreZero(snapshot);
  if (old_val && last_report_counters_were_zero_) {
    ScheduleNextReportLocked();
    return;
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
  GRPC_CLOSURE_INIT(&on_report_done_, OnReportDone, this,
                    grpc_schedule_on_exec_ctx);
  grpc_call_error call_error = grpc_call_start_batch_and_execute(
      parent_->call_, &op, 1, &on_report_done_);
  if (GPR_UNLIKELY(call_error != GRPC_CALL_OK)) {
    gpr_log(GPR_ERROR,
            "[xds_client %p] calld=%p call_error=%d sending client load report",
            xds_client(), this, call_error);
    GPR_ASSERT(GRPC_CALL_OK == call_error);
  }
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnReportDone(
    void* arg, grpc_error* error) {
  Reporter* self = static_cast<Reporter*>(arg);
  self->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&self->on_report_done_, OnReportDoneLocked, self,
                        nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::LrsCallState::Reporter::OnReportDoneLocked(
    void* arg, grpc_error* error) {
  Reporter* self = static_cast<Reporter*>(arg);
  grpc_byte_buffer_destroy(self->parent_->send_message_payload_);
  self->parent_->send_message_payload_ = nullptr;
  if (error != GRPC_ERROR_NONE || !self->IsCurrentReporterOnCall()) {
    // If this reporter is no longer the current one on the call, the reason
    // might be that it was orphaned for a new one due to config update.
    if (!self->IsCurrentReporterOnCall()) {
      self->parent_->MaybeStartReportingLocked();
    }
    self->Unref(DEBUG_LOCATION, "Reporter+report_done");
    return;
  }
  self->ScheduleNextReportLocked();
}

//
// XdsClient::ChannelState::LrsCallState
//

XdsClient::ChannelState::LrsCallState::LrsCallState(
    RefCountedPtr<RetryableCall<LrsCallState>> parent)
    : InternallyRefCounted<LrsCallState>(&grpc_xds_client_trace),
      parent_(std::move(parent)) {
  // Init the LRS call. Note that the call will progress every time there's
  // activity in xds_client()->interested_parties_, which is comprised of
  // the polling entities from client_channel.
  GPR_ASSERT(xds_client() != nullptr);
  GPR_ASSERT(!xds_client()->server_name_.empty());
  call_ = grpc_channel_create_pollset_set_call(
      chand()->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xds_client()->interested_parties_,
      GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_LOAD_STATS_DOT_V2_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS,
      nullptr, GRPC_MILLIS_INF_FUTURE, nullptr);
  GPR_ASSERT(call_ != nullptr);
  // Init the request payload.
  grpc_slice request_payload_slice =
      xds_client()->api_.CreateLrsInitialRequest(xds_client()->server_name_);
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
  op->flags = 0;
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
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 &on_initial_request_sent_);
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
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 &on_response_received_);
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
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 &on_status_received_);
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
  grpc_call_cancel(call_, nullptr);
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
  AdsCallState* ads_calld = chand()->ads_calld_->calld();
  if (ads_calld == nullptr || !ads_calld->seen_response()) return;
  // Start reporting.
  reporter_ = MakeOrphanable<Reporter>(
      Ref(DEBUG_LOCATION, "LRS+load_report+start"), load_reporting_interval_);
}

bool XdsClient::ChannelState::LrsCallState::ShouldSendLoadReports(
    const StringView& cluster_name) const {
  // Only send load reports for the clusters that are asked for by the LRS
  // server.
  return cluster_names_.find(std::string(cluster_name)) != cluster_names_.end();
}

void XdsClient::ChannelState::LrsCallState::OnInitialRequestSent(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  lrs_calld->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&lrs_calld->on_initial_request_sent_,
                        OnInitialRequestSentLocked, lrs_calld, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::LrsCallState::OnInitialRequestSentLocked(
    void* arg, grpc_error* /*error*/) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  // Clear the send_message_payload_.
  grpc_byte_buffer_destroy(lrs_calld->send_message_payload_);
  lrs_calld->send_message_payload_ = nullptr;
  lrs_calld->MaybeStartReportingLocked();
  lrs_calld->Unref(DEBUG_LOCATION, "LRS+OnInitialRequestSentLocked");
}

void XdsClient::ChannelState::LrsCallState::OnResponseReceived(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  lrs_calld->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&lrs_calld->on_response_received_,
                        OnResponseReceivedLocked, lrs_calld, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::LrsCallState::OnResponseReceivedLocked(
    void* arg, grpc_error* /*error*/) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  XdsClient* xds_client = lrs_calld->xds_client();
  // Empty payload means the call was cancelled.
  if (!lrs_calld->IsCurrentCallOnChannel() ||
      lrs_calld->recv_message_payload_ == nullptr) {
    lrs_calld->Unref(DEBUG_LOCATION, "LRS+OnResponseReceivedLocked");
    return;
  }
  // Read the response.
  grpc_byte_buffer_reader bbr;
  grpc_byte_buffer_reader_init(&bbr, lrs_calld->recv_message_payload_);
  grpc_slice response_slice = grpc_byte_buffer_reader_readall(&bbr);
  grpc_byte_buffer_reader_destroy(&bbr);
  grpc_byte_buffer_destroy(lrs_calld->recv_message_payload_);
  lrs_calld->recv_message_payload_ = nullptr;
  // This anonymous lambda is a hack to avoid the usage of goto.
  [&]() {
    // Parse the response.
    std::set<std::string> new_cluster_names;
    grpc_millis new_load_reporting_interval;
    grpc_error* parse_error = xds_client->api_.ParseLrsResponse(
        response_slice, &new_cluster_names, &new_load_reporting_interval);
    if (parse_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR,
              "[xds_client %p] LRS response parsing failed. error=%s",
              xds_client, grpc_error_string(parse_error));
      GRPC_ERROR_UNREF(parse_error);
      return;
    }
    lrs_calld->seen_response_ = true;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] LRS response received, %" PRIuPTR
              " cluster names, load_report_interval=%" PRId64 "ms",
              xds_client, new_cluster_names.size(),
              new_load_reporting_interval);
      size_t i = 0;
      for (const auto& name : new_cluster_names) {
        gpr_log(GPR_INFO, "[xds_client %p] cluster_name %" PRIuPTR ": %s",
                xds_client, i++, name.c_str());
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
                xds_client, GRPC_XDS_MIN_CLIENT_LOAD_REPORTING_INTERVAL_MS);
      }
    }
    // Ignore identical update.
    if (lrs_calld->cluster_names_ == new_cluster_names &&
        lrs_calld->load_reporting_interval_ == new_load_reporting_interval) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] Incoming LRS response identical to current, "
                "ignoring.",
                xds_client);
      }
      return;
    }
    // Stop current load reporting (if any) to adopt the new config.
    lrs_calld->reporter_.reset();
    // Record the new config.
    lrs_calld->cluster_names_ = std::move(new_cluster_names);
    lrs_calld->load_reporting_interval_ = new_load_reporting_interval;
    // Try starting sending load report.
    lrs_calld->MaybeStartReportingLocked();
  }();
  grpc_slice_unref_internal(response_slice);
  if (xds_client->shutting_down_) {
    lrs_calld->Unref(DEBUG_LOCATION,
                     "LRS+OnResponseReceivedLocked+xds_shutdown");
    return;
  }
  // Keep listening for LRS config updates.
  grpc_op op;
  memset(&op, 0, sizeof(op));
  op.op = GRPC_OP_RECV_MESSAGE;
  op.data.recv_message.recv_message = &lrs_calld->recv_message_payload_;
  op.flags = 0;
  op.reserved = nullptr;
  GPR_ASSERT(lrs_calld->call_ != nullptr);
  // Reuse the "OnResponseReceivedLocked" ref taken in ctor.
  GRPC_CLOSURE_INIT(&lrs_calld->on_response_received_, OnResponseReceived,
                    lrs_calld, grpc_schedule_on_exec_ctx);
  const grpc_call_error call_error = grpc_call_start_batch_and_execute(
      lrs_calld->call_, &op, 1, &lrs_calld->on_response_received_);
  GPR_ASSERT(GRPC_CALL_OK == call_error);
}

void XdsClient::ChannelState::LrsCallState::OnStatusReceived(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  lrs_calld->xds_client()->combiner_->Run(
      GRPC_CLOSURE_INIT(&lrs_calld->on_status_received_, OnStatusReceivedLocked,
                        lrs_calld, nullptr),
      GRPC_ERROR_REF(error));
}

void XdsClient::ChannelState::LrsCallState::OnStatusReceivedLocked(
    void* arg, grpc_error* error) {
  LrsCallState* lrs_calld = static_cast<LrsCallState*>(arg);
  XdsClient* xds_client = lrs_calld->xds_client();
  ChannelState* chand = lrs_calld->chand();
  GPR_ASSERT(lrs_calld->call_ != nullptr);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    char* status_details = grpc_slice_to_c_string(lrs_calld->status_details_);
    gpr_log(GPR_INFO,
            "[xds_client %p] LRS call status received. Status = %d, details "
            "= '%s', (chand: %p, calld: %p, call: %p), error '%s'",
            xds_client, lrs_calld->status_code_, status_details, chand,
            lrs_calld, lrs_calld->call_, grpc_error_string(error));
    gpr_free(status_details);
  }
  // Ignore status from a stale call.
  if (lrs_calld->IsCurrentCallOnChannel()) {
    GPR_ASSERT(!xds_client->shutting_down_);
    // Try to restart the call.
    lrs_calld->parent_->OnCallFinishedLocked();
  }
  lrs_calld->Unref(DEBUG_LOCATION, "LRS+OnStatusReceivedLocked");
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

grpc_millis GetRequestTimeout(const grpc_channel_args& args) {
  return grpc_channel_args_find_integer(
      &args, GRPC_ARG_XDS_RESOURCE_DOES_NOT_EXIST_TIMEOUT_MS,
      {15000, 0, INT_MAX});
}

UniquePtr<char> GenerateBuildVersionString() {
  char* build_version_str;
  gpr_asprintf(&build_version_str, "gRPC C-core %s %s", grpc_version_string(),
               GPR_PLATFORM_STRING);
  return UniquePtr<char>(build_version_str);
}

}  // namespace

XdsClient::XdsClient(Combiner* combiner, grpc_pollset_set* interested_parties,
                     StringView server_name,
                     std::unique_ptr<ServiceConfigWatcherInterface> watcher,
                     const grpc_channel_args& channel_args, grpc_error** error)
    : InternallyRefCounted<XdsClient>(&grpc_xds_client_trace),
      request_timeout_(GetRequestTimeout(channel_args)),
      build_version_(GenerateBuildVersionString()),
      combiner_(GRPC_COMBINER_REF(combiner, "xds_client")),
      interested_parties_(interested_parties),
      bootstrap_(XdsBootstrap::ReadFromFile(error)),
      api_(bootstrap_ == nullptr ? nullptr : bootstrap_->node(),
           build_version_.get()),
      server_name_(server_name),
      service_config_watcher_(std::move(watcher)) {
  if (*error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "[xds_client %p] failed to read bootstrap file: %s",
            this, grpc_error_string(*error));
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p] creating channel to %s", this,
            bootstrap_->server().server_uri.c_str());
  }
  grpc_channel_args* new_args = BuildXdsChannelArgs(channel_args);
  grpc_channel* channel = CreateXdsChannel(*bootstrap_, *new_args, error);
  grpc_channel_args_destroy(new_args);
  if (*error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "[xds_client %p] failed to create xds channel: %s", this,
            grpc_error_string(*error));
    return;
  }
  chand_ = MakeOrphanable<ChannelState>(
      Ref(DEBUG_LOCATION, "XdsClient+ChannelState"), channel);
  if (service_config_watcher_ != nullptr) {
    chand_->Subscribe(XdsApi::kLdsTypeUrl, std::string(server_name));
  }
}

XdsClient::~XdsClient() { GRPC_COMBINER_UNREF(combiner_, "xds_client"); }

void XdsClient::Orphan() {
  shutting_down_ = true;
  chand_.reset();
  // We do not clear cluster_map_ and endpoint_map_ if the xds client was
  // created by the XdsResolver because the maps contain refs for watchers which
  // in turn hold refs to the loadbalancing policies. At this point, it is
  // possible for ADS calls to be in progress. Unreffing the loadbalancing
  // policies before those calls are done would lead to issues such as
  // https://github.com/grpc/grpc/issues/20928.
  if (service_config_watcher_ != nullptr) {
    cluster_map_.clear();
    endpoint_map_.clear();
  }
  Unref(DEBUG_LOCATION, "XdsClient::Orphan()");
}

void XdsClient::WatchClusterData(
    StringView cluster_name, std::unique_ptr<ClusterWatcherInterface> watcher) {
  std::string cluster_name_str = std::string(cluster_name);
  ClusterState& cluster_state = cluster_map_[cluster_name_str];
  ClusterWatcherInterface* w = watcher.get();
  cluster_state.watchers[w] = std::move(watcher);
  // If we've already received an CDS update, notify the new watcher
  // immediately.
  if (cluster_state.update.has_value()) {
    w->OnClusterChanged(cluster_state.update.value());
  }
  chand_->Subscribe(XdsApi::kCdsTypeUrl, cluster_name_str);
}

void XdsClient::CancelClusterDataWatch(StringView cluster_name,
                                       ClusterWatcherInterface* watcher) {
  if (shutting_down_) return;
  std::string cluster_name_str = std::string(cluster_name);
  ClusterState& cluster_state = cluster_map_[cluster_name_str];
  auto it = cluster_state.watchers.find(watcher);
  if (it != cluster_state.watchers.end()) {
    cluster_state.watchers.erase(it);
    if (cluster_state.watchers.empty()) {
      cluster_map_.erase(cluster_name_str);
      chand_->Unsubscribe(XdsApi::kCdsTypeUrl, cluster_name_str);
    }
  }
}

void XdsClient::WatchEndpointData(
    StringView eds_service_name,
    std::unique_ptr<EndpointWatcherInterface> watcher) {
  std::string eds_service_name_str = std::string(eds_service_name);
  EndpointState& endpoint_state = endpoint_map_[eds_service_name_str];
  EndpointWatcherInterface* w = watcher.get();
  endpoint_state.watchers[w] = std::move(watcher);
  // If we've already received an EDS update, notify the new watcher
  // immediately.
  if (!endpoint_state.update.priority_list_update.empty()) {
    w->OnEndpointChanged(endpoint_state.update);
  }
  chand_->Subscribe(XdsApi::kEdsTypeUrl, eds_service_name_str);
}

void XdsClient::CancelEndpointDataWatch(StringView eds_service_name,
                                        EndpointWatcherInterface* watcher) {
  if (shutting_down_) return;
  std::string eds_service_name_str = std::string(eds_service_name);
  EndpointState& endpoint_state = endpoint_map_[eds_service_name_str];
  auto it = endpoint_state.watchers.find(watcher);
  if (it != endpoint_state.watchers.end()) {
    endpoint_state.watchers.erase(it);
    if (endpoint_state.watchers.empty()) {
      endpoint_map_.erase(eds_service_name_str);
      chand_->Unsubscribe(XdsApi::kEdsTypeUrl, eds_service_name_str);
    }
  }
}

RefCountedPtr<XdsClusterDropStats> XdsClient::AddClusterDropStats(
    StringView lrs_server, StringView cluster_name,
    StringView eds_service_name) {
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  // We jump through some hoops here to make sure that the StringViews
  // stored in the XdsClusterDropStats object point to the strings
  // in the load_report_map_ key, so that they have the same lifetime.
  auto it = load_report_map_
                .emplace(std::make_pair(std::move(key), LoadReportState()))
                .first;
  auto cluster_drop_stats = MakeRefCounted<XdsClusterDropStats>(
      Ref(DEBUG_LOCATION, "DropStats"), lrs_server,
      it->first.first /*cluster_name*/, it->first.second /*eds_service_name*/);
  it->second.drop_stats.insert(cluster_drop_stats.get());
  chand_->MaybeStartLrsCall();
  return cluster_drop_stats;
}

void XdsClient::RemoveClusterDropStats(
    StringView /*lrs_server*/, StringView cluster_name,
    StringView eds_service_name, XdsClusterDropStats* cluster_drop_stats) {
  auto load_report_it = load_report_map_.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (load_report_it == load_report_map_.end()) return;
  LoadReportState& load_report_state = load_report_it->second;
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  // TODO(roth): In principle, we should try to send a final load report
  // containing whatever final stats have been accumulated since the
  // last load report.
  auto it = load_report_state.drop_stats.find(cluster_drop_stats);
  if (it != load_report_state.drop_stats.end()) {
    load_report_state.drop_stats.erase(it);
    if (load_report_state.drop_stats.empty() &&
        load_report_state.locality_stats.empty()) {
      load_report_map_.erase(load_report_it);
      if (chand_ != nullptr && load_report_map_.empty()) {
        chand_->StopLrsCall();
      }
    }
  }
}

RefCountedPtr<XdsClusterLocalityStats> XdsClient::AddClusterLocalityStats(
    StringView lrs_server, StringView cluster_name, StringView eds_service_name,
    RefCountedPtr<XdsLocalityName> locality) {
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  auto key =
      std::make_pair(std::string(cluster_name), std::string(eds_service_name));
  // We jump through some hoops here to make sure that the StringViews
  // stored in the XdsClusterLocalityStats object point to the strings
  // in the load_report_map_ key, so that they have the same lifetime.
  auto it = load_report_map_
                .emplace(std::make_pair(std::move(key), LoadReportState()))
                .first;
  auto cluster_locality_stats = MakeRefCounted<XdsClusterLocalityStats>(
      Ref(DEBUG_LOCATION, "LocalityStats"), lrs_server,
      it->first.first /*cluster_name*/, it->first.second /*eds_service_name*/,
      locality);
  it->second.locality_stats[std::move(locality)].insert(
      cluster_locality_stats.get());
  chand_->MaybeStartLrsCall();
  return cluster_locality_stats;
}

void XdsClient::RemoveClusterLocalityStats(
    StringView /*lrs_server*/, StringView cluster_name,
    StringView eds_service_name, const RefCountedPtr<XdsLocalityName>& locality,
    XdsClusterLocalityStats* cluster_locality_stats) {
  auto load_report_it = load_report_map_.find(
      std::make_pair(std::string(cluster_name), std::string(eds_service_name)));
  if (load_report_it == load_report_map_.end()) return;
  LoadReportState& load_report_state = load_report_it->second;
  // TODO(roth): When we add support for direct federation, use the
  // server name specified in lrs_server.
  // TODO(roth): In principle, we should try to send a final load report
  // containing whatever final stats have been accumulated since the
  // last load report.
  auto locality_it = load_report_state.locality_stats.find(locality);
  if (locality_it == load_report_state.locality_stats.end()) return;
  auto& locality_set = locality_it->second;
  auto it = locality_set.find(cluster_locality_stats);
  if (it != locality_set.end()) {
    locality_set.erase(it);
    if (locality_set.empty()) {
      load_report_state.locality_stats.erase(locality_it);
      if (load_report_state.locality_stats.empty() &&
          load_report_state.drop_stats.empty()) {
        load_report_map_.erase(load_report_it);
        if (chand_ != nullptr && load_report_map_.empty()) {
          chand_->StopLrsCall();
        }
      }
    }
  }
}

void XdsClient::ResetBackoff() {
  if (chand_ != nullptr) {
    grpc_channel_reset_connect_backoff(chand_->channel());
  }
}

grpc_error* XdsClient::CreateServiceConfig(
    const std::string& cluster_name,
    RefCountedPtr<ServiceConfig>* service_config) const {
  char* json;
  gpr_asprintf(&json,
               "{\n"
               "  \"loadBalancingConfig\":[\n"
               "    { \"cds_experimental\":{\n"
               "      \"cluster\": \"%s\"\n"
               "    } }\n"
               "  ]\n"
               "}",
               cluster_name.c_str());
  grpc_error* error = GRPC_ERROR_NONE;
  *service_config = ServiceConfig::Create(json, &error);
  gpr_free(json);
  return error;
}

XdsApi::ClusterLoadReportMap XdsClient::BuildLoadReportSnapshot() {
  XdsApi::ClusterLoadReportMap snapshot_map;
  for (auto& p : load_report_map_) {
    const auto& cluster_key = p.first;  // cluster and EDS service name
    LoadReportState& load_report = p.second;
    XdsApi::ClusterLoadReport& snapshot = snapshot_map[cluster_key];
    // Aggregate drop stats.
    for (auto& drop_stats : load_report.drop_stats) {
      for (const auto& p : drop_stats->GetSnapshotAndReset()) {
        snapshot.dropped_requests[p.first] += p.second;
      }
    }
    // Aggregate locality stats.
    for (auto& p : load_report.locality_stats) {
      XdsLocalityName* locality_name = p.first.get();
      auto& locality_stats_set = p.second;
      XdsClusterLocalityStats::Snapshot& locality_snapshot =
          snapshot.locality_stats[locality_name];
      for (auto& locality_stats : locality_stats_set) {
        locality_snapshot += locality_stats->GetSnapshotAndReset();
      }
    }
    // Compute load report interval.
    const grpc_millis now = ExecCtx::Get()->Now();
    snapshot.load_report_interval = now - load_report.last_report_time;
    load_report.last_report_time = now;
  }
  return snapshot_map;
}

void XdsClient::NotifyOnError(grpc_error* error) {
  if (service_config_watcher_ != nullptr) {
    service_config_watcher_->OnError(GRPC_ERROR_REF(error));
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

void* XdsClient::ChannelArgCopy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Ref(DEBUG_LOCATION, "channel arg").release();
  return p;
}

void XdsClient::ChannelArgDestroy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Unref(DEBUG_LOCATION, "channel arg");
}

int XdsClient::ChannelArgCmp(void* p, void* q) { return GPR_ICMP(p, q); }

const grpc_arg_pointer_vtable XdsClient::kXdsClientVtable = {
    XdsClient::ChannelArgCopy, XdsClient::ChannelArgDestroy,
    XdsClient::ChannelArgCmp};

grpc_arg XdsClient::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(const_cast<char*>(GRPC_ARG_XDS_CLIENT),
                                         const_cast<XdsClient*>(this),
                                         &XdsClient::kXdsClientVtable);
}

RefCountedPtr<XdsClient> XdsClient::GetFromChannelArgs(
    const grpc_channel_args& args) {
  XdsClient* xds_client =
      grpc_channel_args_find_pointer<XdsClient>(&args, GRPC_ARG_XDS_CLIENT);
  if (xds_client != nullptr) return xds_client->Ref();
  return nullptr;
}

}  // namespace grpc_core
