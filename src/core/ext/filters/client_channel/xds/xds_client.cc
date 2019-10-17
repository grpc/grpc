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

 private:
  static void OnResponseReceived(void* arg, grpc_error* error);
  static void OnStatusReceived(void* arg, grpc_error* error);
  static void OnResponseReceivedLocked(void* arg, grpc_error* error);
  static void OnStatusReceivedLocked(void* arg, grpc_error* error);

  bool IsCurrentCallOnChannel() const;

  // The owning RetryableCall<>.
  RefCountedPtr<RetryableCall<AdsCallState>> parent_;
  bool seen_response_ = false;

  // Always non-NULL.
  grpc_call* call_;

  // recv_initial_metadata
  grpc_metadata_array initial_metadata_recv_;

  // send_message
  grpc_byte_buffer* send_message_payload_ = nullptr;

  // recv_message
  grpc_byte_buffer* recv_message_payload_ = nullptr;
  grpc_closure on_response_received_;

  // recv_trailing_metadata
  grpc_metadata_array trailing_metadata_recv_;
  grpc_status_code status_code_;
  grpc_slice status_details_;
  grpc_closure on_status_received_;
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
  UniquePtr<char> cluster_name_;
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
  };
  // Channel args to add.
  InlinedVector<grpc_arg, 2> args_to_add;
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
                                      const grpc_channel_args& args)
    : InternallyRefCounted<ChannelState>(&grpc_xds_client_trace),
      xds_client_(std::move(xds_client)) {
  grpc_channel_args* new_args = BuildXdsChannelArgs(args);
  channel_ = CreateXdsChannel(*xds_client_->bootstrap_, *new_args);
  grpc_channel_args_destroy(new_args);
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

void XdsClient::ChannelState::MaybeStartAdsCall() {
  if (ads_calld_ != nullptr) return;
  ads_calld_.reset(New<RetryableCall<AdsCallState>>(
      Ref(DEBUG_LOCATION, "ChannelState+ads")));
}

void XdsClient::ChannelState::StopAdsCall() { ads_calld_.reset(); }

void XdsClient::ChannelState::MaybeStartLrsCall() {
  if (lrs_calld_ != nullptr) return;
  lrs_calld_.reset(New<RetryableCall<LrsCallState>>(
      Ref(DEBUG_LOCATION, "ChannelState+lrs")));
}

void XdsClient::ChannelState::StopLrsCall() { lrs_calld_.reset(); }

void XdsClient::ChannelState::StartConnectivityWatchLocked() {
  grpc_channel_element* client_channel_elem =
      grpc_channel_stack_last_element(grpc_channel_get_channel_stack(channel_));
  GPR_ASSERT(client_channel_elem->filter == &grpc_client_channel_filter);
  watcher_ = New<StateWatcher>(Ref());
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
  GPR_ASSERT(xds_client()->server_name_ != nullptr);
  GPR_ASSERT(*xds_client()->server_name_.get() != '\0');
  // Create a call with the specified method name.
  call_ = grpc_channel_create_pollset_set_call(
      chand()->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xds_client()->interested_parties_,
      GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_DISCOVERY_DOT_V2_DOT_AGGREGATEDDISCOVERYSERVICE_SLASH_STREAMAGGREGATEDRESOURCES,
      nullptr, GRPC_MILLIS_INF_FUTURE, nullptr);
  GPR_ASSERT(call_ != nullptr);
  // Init the request payload.
  grpc_slice request_payload_slice = XdsEdsRequestCreateAndEncode(
      xds_client()->server_name_.get(), xds_client()->bootstrap_->node(),
      xds_client()->build_version_.get());
  send_message_payload_ =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);
  grpc_slice_unref_internal(request_payload_slice);
  // Init other data associated with the call.
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
  // Op: send request message.
  GPR_ASSERT(send_message_payload_ != nullptr);
  op->op = GRPC_OP_SEND_MESSAGE;
  op->data.send_message.send_message = send_message_payload_;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  call_error = grpc_call_start_batch_and_execute(call_, ops, (size_t)(op - ops),
                                                 nullptr);
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
  // Note that the initial ref is hold by on_status_received_. So the
  // corresponding unref happens in on_status_received_ instead of here.
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
    void* arg, grpc_error* error) {
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
  // This anonymous lambda is a hack to avoid the usage of goto.
  [&]() {
    // Parse the response.
    EdsUpdate update;
    grpc_error* parse_error =
        XdsEdsResponseDecodeAndParse(response_slice, &update);
    if (parse_error != GRPC_ERROR_NONE) {
      gpr_log(GPR_ERROR,
              "[xds_client %p] ADS response parsing failed. error=%s",
              xds_client, grpc_error_string(parse_error));
      GRPC_ERROR_UNREF(parse_error);
      return;
    }
    if (update.priority_list_update.empty() && !update.drop_all) {
      char* response_slice_str =
          grpc_dump_slice(response_slice, GPR_DUMP_ASCII | GPR_DUMP_HEX);
      gpr_log(GPR_ERROR,
              "[xds_client %p] ADS response '%s' doesn't contain any valid "
              "locality but doesn't require to drop all calls. Ignoring.",
              xds_client, response_slice_str);
      gpr_free(response_slice_str);
      return;
    }
    ads_calld->seen_response_ = true;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO,
              "[xds_client %p] ADS response with %" PRIuPTR
              " priorities and %" PRIuPTR
              " drop categories received (drop_all=%d)",
              xds_client, update.priority_list_update.size(),
              update.drop_config->drop_category_list().size(), update.drop_all);
      for (size_t priority = 0; priority < update.priority_list_update.size();
           ++priority) {
        const auto* locality_map_update =
            update.priority_list_update.Find(static_cast<uint32_t>(priority));
        gpr_log(GPR_INFO,
                "[xds_client %p] Priority %" PRIuPTR " contains %" PRIuPTR
                " localities",
                xds_client, priority, locality_map_update->size());
        size_t locality_count = 0;
        for (const auto& p : locality_map_update->localities) {
          const auto& locality = p.second;
          gpr_log(GPR_INFO,
                  "[xds_client %p] Priority %" PRIuPTR ", locality %" PRIuPTR
                  " %s contains %" PRIuPTR " server addresses",
                  xds_client, priority, locality_count,
                  locality.name->AsHumanReadableString(),
                  locality.serverlist.size());
          for (size_t i = 0; i < locality.serverlist.size(); ++i) {
            char* ipport;
            grpc_sockaddr_to_string(&ipport, &locality.serverlist[i].address(),
                                    false);
            gpr_log(GPR_INFO,
                    "[xds_client %p] Priority %" PRIuPTR ", locality %" PRIuPTR
                    " %s, server address %" PRIuPTR ": %s",
                    xds_client, priority, locality_count,
                    locality.name->AsHumanReadableString(), i, ipport);
            gpr_free(ipport);
          }
          ++locality_count;
        }
      }
      for (size_t i = 0; i < update.drop_config->drop_category_list().size();
           ++i) {
        const XdsDropConfig::DropCategory& drop_category =
            update.drop_config->drop_category_list()[i];
        gpr_log(GPR_INFO,
                "[xds_client %p] Drop category %s has drop rate %d per million",
                xds_client, drop_category.name.get(),
                drop_category.parts_per_million);
      }
    }
    // Start load reporting if needed.
    LrsCallState* lrs_calld = ads_calld->chand()->lrs_calld_->calld();
    if (lrs_calld != nullptr) lrs_calld->MaybeStartReportingLocked();
    // Ignore identical update.
    const EdsUpdate& prev_update = xds_client->cluster_state_.eds_update;
    const bool priority_list_changed =
        prev_update.priority_list_update != update.priority_list_update;
    const bool drop_config_changed =
        prev_update.drop_config == nullptr ||
        *prev_update.drop_config != *update.drop_config;
    if (!priority_list_changed && !drop_config_changed) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
        gpr_log(GPR_INFO,
                "[xds_client %p] EDS update identical to current, ignoring.",
                xds_client);
      }
      return;
    }
    // Update the cluster state.
    ClusterState& cluster_state = xds_client->cluster_state_;
    cluster_state.eds_update = std::move(update);
    // Notify all watchers.
    for (const auto& p : cluster_state.endpoint_watchers) {
      p.first->OnEndpointChanged(cluster_state.eds_update);
    }
  }();
  grpc_slice_unref_internal(response_slice);
  if (xds_client->shutting_down_) {
    ads_calld->Unref(DEBUG_LOCATION,
                     "ADS+OnResponseReceivedLocked+xds_shutdown");
    return;
  }
  // Keep listening for serverlist updates.
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

void XdsClient::ChannelState::LrsCallState::Reporter::SendReportLocked() {
  // Create a request that contains the load report.
  // TODO(roth): Currently, it is not possible to have multiple client
  // stats objects for a given cluster.  However, in the future, we may
  // run into cases where this happens (e.g., due to graceful LB policy
  // switching).  If/when this becomes a problem, replace this assertion
  // with code to merge data from multiple client stats objects.
  GPR_ASSERT(xds_client()->cluster_state_.client_stats.size() == 1);
  auto* client_stats = *xds_client()->cluster_state_.client_stats.begin();
  grpc_slice request_payload_slice =
      XdsLrsRequestCreateAndEncode(parent_->cluster_name_.get(), client_stats);
  // Skip client load report if the counters were all zero in the last
  // report and they are still zero in this one.
  const bool old_val = last_report_counters_were_zero_;
  last_report_counters_were_zero_ = static_cast<bool>(
      grpc_slice_eq(request_payload_slice, grpc_empty_slice()));
  if (old_val && last_report_counters_were_zero_) {
    ScheduleNextReportLocked();
    return;
  }
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
  GPR_ASSERT(xds_client()->server_name_ != nullptr);
  GPR_ASSERT(*xds_client()->server_name_.get() != '\0');
  call_ = grpc_channel_create_pollset_set_call(
      chand()->channel_, nullptr, GRPC_PROPAGATE_DEFAULTS,
      xds_client()->interested_parties_,
      GRPC_MDSTR_SLASH_ENVOY_DOT_SERVICE_DOT_LOAD_STATS_DOT_V2_DOT_LOADREPORTINGSERVICE_SLASH_STREAMLOADSTATS,
      nullptr, GRPC_MILLIS_INF_FUTURE, nullptr);
  GPR_ASSERT(call_ != nullptr);
  // Init the request payload.
  grpc_slice request_payload_slice = XdsLrsRequestCreateAndEncode(
      xds_client()->server_name_.get(), xds_client()->bootstrap_->node(),
      xds_client()->build_version_.get());
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
  for (auto* client_stats : chand()->xds_client_->cluster_state_.client_stats) {
    client_stats->MaybeInitLastReportTime();
  }
  reporter_ = MakeOrphanable<Reporter>(
      Ref(DEBUG_LOCATION, "LRS+load_report+start"), load_reporting_interval_);
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
    void* arg, grpc_error* error) {
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
    void* arg, grpc_error* error) {
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
    UniquePtr<char> new_cluster_name;
    grpc_millis new_load_reporting_interval;
    grpc_error* parse_error = XdsLrsResponseDecodeAndParse(
        response_slice, &new_cluster_name, &new_load_reporting_interval);
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
              "[xds_client %p] LRS response received, cluster_name=%s, "
              "load_report_interval=%" PRId64 "ms",
              xds_client, new_cluster_name.get(), new_load_reporting_interval);
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
    if (lrs_calld->load_reporting_interval_ == new_load_reporting_interval &&
        strcmp(lrs_calld->cluster_name_.get(), new_cluster_name.get()) == 0) {
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
    lrs_calld->cluster_name_ = std::move(new_cluster_name);
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

UniquePtr<char> GenerateBuildVersionString() {
  char* build_version_str;
  gpr_asprintf(&build_version_str, "gRPC C-core %s %s", grpc_version_string(),
               GPR_PLATFORM_STRING);
  return UniquePtr<char>(build_version_str);
}

}  // namespace

XdsClient::XdsClient(Combiner* combiner, grpc_pollset_set* interested_parties,
                     StringView server_name,
                     UniquePtr<ServiceConfigWatcherInterface> watcher,
                     const grpc_channel_args& channel_args, grpc_error** error)
    : build_version_(GenerateBuildVersionString()),
      combiner_(GRPC_COMBINER_REF(combiner, "xds_client")),
      interested_parties_(interested_parties),
      bootstrap_(XdsBootstrap::ReadFromFile(error)),
      server_name_(server_name.dup()),
      service_config_watcher_(std::move(watcher)) {
  if (*error != GRPC_ERROR_NONE) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
      gpr_log(GPR_INFO, "[xds_client %p: failed to read bootstrap file: %s",
              this, grpc_error_string(*error));
    }
    return;
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_xds_client_trace)) {
    gpr_log(GPR_INFO, "[xds_client %p: creating channel to %s", this,
            bootstrap_->server_uri());
  }
  chand_ = MakeOrphanable<ChannelState>(
      Ref(DEBUG_LOCATION, "XdsClient+ChannelState"), channel_args);
  if (service_config_watcher_ != nullptr) {
    // TODO(juanlishen): Start LDS call and do not return service config
    // until we get the first LDS response.
    GRPC_CLOSURE_INIT(&service_config_notify_, NotifyOnServiceConfig,
                      Ref().release(), nullptr);
    combiner_->Run(&service_config_notify_, GRPC_ERROR_NONE);
  }
}

XdsClient::~XdsClient() { GRPC_COMBINER_UNREF(combiner_, "xds_client"); }

void XdsClient::Orphan() {
  shutting_down_ = true;
  chand_.reset();
  Unref(DEBUG_LOCATION, "XdsClient::Orphan()");
}

void XdsClient::WatchClusterData(StringView cluster,
                                 UniquePtr<ClusterWatcherInterface> watcher) {
  // TODO(juanlishen): Implement.
}

void XdsClient::CancelClusterDataWatch(StringView cluster,
                                       ClusterWatcherInterface* watcher) {
  // TODO(juanlishen): Implement.
}

void XdsClient::WatchEndpointData(StringView cluster,
                                  UniquePtr<EndpointWatcherInterface> watcher) {
  EndpointWatcherInterface* w = watcher.get();
  cluster_state_.endpoint_watchers[w] = std::move(watcher);
  // If we've already received an EDS update, notify the new watcher
  // immediately.
  if (!cluster_state_.eds_update.priority_list_update.empty()) {
    w->OnEndpointChanged(cluster_state_.eds_update);
  }
  chand_->MaybeStartAdsCall();
}

void XdsClient::CancelEndpointDataWatch(StringView cluster,
                                        EndpointWatcherInterface* watcher) {
  auto it = cluster_state_.endpoint_watchers.find(watcher);
  if (it != cluster_state_.endpoint_watchers.end()) {
    cluster_state_.endpoint_watchers.erase(it);
  }
  if (chand_ != nullptr && cluster_state_.endpoint_watchers.empty()) {
    chand_->StopAdsCall();
  }
}

void XdsClient::AddClientStats(StringView cluster,
                               XdsClientStats* client_stats) {
  cluster_state_.client_stats.insert(client_stats);
  chand_->MaybeStartLrsCall();
}

void XdsClient::RemoveClientStats(StringView cluster,
                                  XdsClientStats* client_stats) {
  // TODO(roth): In principle, we should try to send a final load report
  // containing whatever final stats have been accumulated since the
  // last load report.
  auto it = cluster_state_.client_stats.find(client_stats);
  if (it != cluster_state_.client_stats.end()) {
    cluster_state_.client_stats.erase(it);
  }
  if (chand_ != nullptr && cluster_state_.client_stats.empty()) {
    chand_->StopLrsCall();
  }
}

void XdsClient::ResetBackoff() {
  if (chand_ != nullptr) {
    grpc_channel_reset_connect_backoff(chand_->channel());
  }
}

void XdsClient::NotifyOnError(grpc_error* error) {
  if (service_config_watcher_ != nullptr) {
    service_config_watcher_->OnError(GRPC_ERROR_REF(error));
  }
  for (const auto& p : cluster_state_.cluster_watchers) {
    p.first->OnError(GRPC_ERROR_REF(error));
  }
  for (const auto& p : cluster_state_.endpoint_watchers) {
    p.first->OnError(GRPC_ERROR_REF(error));
  }
  GRPC_ERROR_UNREF(error);
}

void XdsClient::NotifyOnServiceConfig(void* arg, grpc_error* error) {
  XdsClient* self = static_cast<XdsClient*>(arg);
  // TODO(roth): When we add support for WeightedClusters, select the
  // LB policy based on that functionality.
  static const char* json =
      "{\n"
      "  \"loadBalancingConfig\":[\n"
      "    { \"xds_experimental\":{} }\n"
      "  ]\n"
      "}";
  RefCountedPtr<ServiceConfig> service_config =
      ServiceConfig::Create(json, &error);
  if (error != GRPC_ERROR_NONE) {
    self->service_config_watcher_->OnError(error);
  } else {
    self->service_config_watcher_->OnServiceConfigChanged(
        std::move(service_config));
  }
  self->Unref();
}

void* XdsClient::ChannelArgCopy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Ref().release();
  return p;
}

void XdsClient::ChannelArgDestroy(void* p) {
  XdsClient* xds_client = static_cast<XdsClient*>(p);
  xds_client->Unref();
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
