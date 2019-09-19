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

#include <stdint.h>
#include <stdio.h>

#include "src/core/ext/filters/client_channel/health/health_check_client.h"

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/status_metadata.h"
#include "src/proto/grpc/health/v1/health.upb.h"

#define HEALTH_CHECK_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define HEALTH_CHECK_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define HEALTH_CHECK_RECONNECT_MAX_BACKOFF_SECONDS 120
#define HEALTH_CHECK_RECONNECT_JITTER 0.2

namespace grpc_core {

TraceFlag grpc_health_check_client_trace(false, "health_check_client");

//
// HealthCheckClient
//

HealthCheckClient::HealthCheckClient(
    const char* service_name,
    RefCountedPtr<ConnectedSubchannel> connected_subchannel,
    grpc_pollset_set* interested_parties,
    RefCountedPtr<channelz::SubchannelNode> channelz_node)
    : InternallyRefCounted<HealthCheckClient>(&grpc_health_check_client_trace),
      service_name_(service_name),
      connected_subchannel_(std::move(connected_subchannel)),
      interested_parties_(interested_parties),
      channelz_node_(std::move(channelz_node)),
      retry_backoff_(
          BackOff::Options()
              .set_initial_backoff(
                  HEALTH_CHECK_INITIAL_CONNECT_BACKOFF_SECONDS * 1000)
              .set_multiplier(HEALTH_CHECK_RECONNECT_BACKOFF_MULTIPLIER)
              .set_jitter(HEALTH_CHECK_RECONNECT_JITTER)
              .set_max_backoff(HEALTH_CHECK_RECONNECT_MAX_BACKOFF_SECONDS *
                               1000)) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "created HealthCheckClient %p", this);
  }
  GRPC_CLOSURE_INIT(&retry_timer_callback_, OnRetryTimer, this,
                    grpc_schedule_on_exec_ctx);
  StartCall();
}

HealthCheckClient::~HealthCheckClient() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "destroying HealthCheckClient %p", this);
  }
  GRPC_ERROR_UNREF(error_);
}

void HealthCheckClient::NotifyOnHealthChange(grpc_connectivity_state* state,
                                             grpc_closure* closure) {
  MutexLock lock(&mu_);
  GPR_ASSERT(notify_state_ == nullptr);
  if (*state != state_) {
    *state = state_;
    GRPC_CLOSURE_SCHED(closure, GRPC_ERROR_REF(error_));
    return;
  }
  notify_state_ = state;
  on_health_changed_ = closure;
}

void HealthCheckClient::SetHealthStatus(grpc_connectivity_state state,
                                        grpc_error* error) {
  MutexLock lock(&mu_);
  SetHealthStatusLocked(state, error);
}

void HealthCheckClient::SetHealthStatusLocked(grpc_connectivity_state state,
                                              grpc_error* error) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthCheckClient %p: setting state=%d error=%s", this,
            state, grpc_error_string(error));
  }
  if (notify_state_ != nullptr && *notify_state_ != state) {
    *notify_state_ = state;
    notify_state_ = nullptr;
    GRPC_CLOSURE_SCHED(on_health_changed_, GRPC_ERROR_REF(error));
    on_health_changed_ = nullptr;
  }
  state_ = state;
  GRPC_ERROR_UNREF(error_);
  error_ = error;
}

void HealthCheckClient::Orphan() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthCheckClient %p: shutting down", this);
  }
  {
    MutexLock lock(&mu_);
    if (on_health_changed_ != nullptr) {
      *notify_state_ = GRPC_CHANNEL_SHUTDOWN;
      notify_state_ = nullptr;
      GRPC_CLOSURE_SCHED(on_health_changed_, GRPC_ERROR_NONE);
      on_health_changed_ = nullptr;
    }
    shutting_down_ = true;
    call_state_.reset();
    if (retry_timer_callback_pending_) {
      grpc_timer_cancel(&retry_timer_);
    }
  }
  Unref(DEBUG_LOCATION, "orphan");
}

void HealthCheckClient::StartCall() {
  MutexLock lock(&mu_);
  StartCallLocked();
}

void HealthCheckClient::StartCallLocked() {
  if (shutting_down_) return;
  GPR_ASSERT(call_state_ == nullptr);
  SetHealthStatusLocked(GRPC_CHANNEL_CONNECTING, GRPC_ERROR_NONE);
  call_state_ = MakeOrphanable<CallState>(Ref(), interested_parties_);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthCheckClient %p: created CallState %p", this,
            call_state_.get());
  }
  call_state_->StartCall();
}

void HealthCheckClient::StartRetryTimer() {
  MutexLock lock(&mu_);
  SetHealthStatusLocked(
      GRPC_CHANNEL_TRANSIENT_FAILURE,
      GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "health check call failed; will retry after backoff"));
  grpc_millis next_try = retry_backoff_.NextAttemptTime();
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthCheckClient %p: health check call lost...", this);
    grpc_millis timeout = next_try - ExecCtx::Get()->Now();
    if (timeout > 0) {
      gpr_log(GPR_INFO,
              "HealthCheckClient %p: ... will retry in %" PRId64 "ms.", this,
              timeout);
    } else {
      gpr_log(GPR_INFO, "HealthCheckClient %p: ... retrying immediately.",
              this);
    }
  }
  // Ref for callback, tracked manually.
  Ref(DEBUG_LOCATION, "health_retry_timer").release();
  retry_timer_callback_pending_ = true;
  grpc_timer_init(&retry_timer_, next_try, &retry_timer_callback_);
}

void HealthCheckClient::OnRetryTimer(void* arg, grpc_error* error) {
  HealthCheckClient* self = static_cast<HealthCheckClient*>(arg);
  {
    MutexLock lock(&self->mu_);
    self->retry_timer_callback_pending_ = false;
    if (!self->shutting_down_ && error == GRPC_ERROR_NONE &&
        self->call_state_ == nullptr) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
        gpr_log(GPR_INFO, "HealthCheckClient %p: restarting health check call",
                self);
      }
      self->StartCallLocked();
    }
  }
  self->Unref(DEBUG_LOCATION, "health_retry_timer");
}

//
// protobuf helpers
//

namespace {

void EncodeRequest(const char* service_name,
                   ManualConstructor<SliceBufferByteStream>* send_message) {
  upb::Arena arena;
  grpc_health_v1_HealthCheckRequest* request_struct =
      grpc_health_v1_HealthCheckRequest_new(arena.ptr());
  grpc_health_v1_HealthCheckRequest_set_service(
      request_struct, upb_strview_makez(service_name));
  size_t buf_length;
  char* buf = grpc_health_v1_HealthCheckRequest_serialize(
      request_struct, arena.ptr(), &buf_length);
  grpc_slice request_slice = GRPC_SLICE_MALLOC(buf_length);
  memcpy(GRPC_SLICE_START_PTR(request_slice), buf, buf_length);
  grpc_slice_buffer slice_buffer;
  grpc_slice_buffer_init(&slice_buffer);
  grpc_slice_buffer_add(&slice_buffer, request_slice);
  send_message->Init(&slice_buffer, 0);
  grpc_slice_buffer_destroy_internal(&slice_buffer);
}

// Returns true if healthy.
// If there was an error parsing the response, sets *error and returns false.
bool DecodeResponse(grpc_slice_buffer* slice_buffer, grpc_error** error) {
  // If message is empty, assume unhealthy.
  if (slice_buffer->length == 0) {
    *error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("health check response was empty");
    return false;
  }
  // Concatenate the slices to form a single string.
  UniquePtr<uint8_t> recv_message_deleter;
  uint8_t* recv_message;
  if (slice_buffer->count == 1) {
    recv_message = GRPC_SLICE_START_PTR(slice_buffer->slices[0]);
  } else {
    recv_message = static_cast<uint8_t*>(gpr_malloc(slice_buffer->length));
    recv_message_deleter.reset(recv_message);
    size_t offset = 0;
    for (size_t i = 0; i < slice_buffer->count; ++i) {
      memcpy(recv_message + offset,
             GRPC_SLICE_START_PTR(slice_buffer->slices[i]),
             GRPC_SLICE_LENGTH(slice_buffer->slices[i]));
      offset += GRPC_SLICE_LENGTH(slice_buffer->slices[i]);
    }
  }
  // Deserialize message.
  upb::Arena arena;
  grpc_health_v1_HealthCheckResponse* response_struct =
      grpc_health_v1_HealthCheckResponse_parse(
          reinterpret_cast<char*>(recv_message), slice_buffer->length,
          arena.ptr());
  if (response_struct == nullptr) {
    // Can't parse message; assume unhealthy.
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "cannot parse health check response");
    return false;
  }
  int32_t status = grpc_health_v1_HealthCheckResponse_status(response_struct);
  return status == grpc_health_v1_HealthCheckResponse_SERVING;
}

}  // namespace

//
// HealthCheckClient::CallState
//

HealthCheckClient::CallState::CallState(
    RefCountedPtr<HealthCheckClient> health_check_client,
    grpc_pollset_set* interested_parties)
    : health_check_client_(std::move(health_check_client)),
      pollent_(grpc_polling_entity_create_from_pollset_set(interested_parties)),
      arena_(Arena::Create(health_check_client_->connected_subchannel_
                               ->GetInitialCallSizeEstimate(0))),
      payload_(context_) {}

HealthCheckClient::CallState::~CallState() {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO, "HealthCheckClient %p: destroying CallState %p",
            health_check_client_.get(), this);
  }
  for (size_t i = 0; i < GRPC_CONTEXT_COUNT; i++) {
    if (context_[i].destroy != nullptr) {
      context_[i].destroy(context_[i].value);
    }
  }
  // Unset the call combiner cancellation closure.  This has the
  // effect of scheduling the previously set cancellation closure, if
  // any, so that it can release any internal references it may be
  // holding to the call stack. Also flush the closures on exec_ctx so that
  // filters that schedule cancel notification closures on exec_ctx do not
  // need to take a ref of the call stack to guarantee closure liveness.
  call_combiner_.SetNotifyOnCancel(nullptr);
  ExecCtx::Get()->Flush();
  arena_->Destroy();
}

void HealthCheckClient::CallState::Orphan() {
  call_combiner_.Cancel(GRPC_ERROR_CANCELLED);
  Cancel();
}

void HealthCheckClient::CallState::StartCall() {
  SubchannelCall::Args args = {
      health_check_client_->connected_subchannel_,
      &pollent_,
      GRPC_MDSTR_SLASH_GRPC_DOT_HEALTH_DOT_V1_DOT_HEALTH_SLASH_WATCH,
      gpr_get_cycle_counter(),  // start_time
      GRPC_MILLIS_INF_FUTURE,   // deadline
      arena_,
      context_,
      &call_combiner_,
      0,  // parent_data_size
  };
  grpc_error* error = GRPC_ERROR_NONE;
  call_ = SubchannelCall::Create(std::move(args), &error).release();
  // Register after-destruction callback.
  GRPC_CLOSURE_INIT(&after_call_stack_destruction_, AfterCallStackDestruction,
                    this, grpc_schedule_on_exec_ctx);
  call_->SetAfterCallStackDestroy(&after_call_stack_destruction_);
  // Check if creation failed.
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "HealthCheckClient %p CallState %p: error creating health "
            "checking call on subchannel (%s); will retry",
            health_check_client_.get(), this, grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    // Schedule instead of running directly, since we must not be
    // holding health_check_client_->mu_ when CallEnded() is called.
    call_->Ref(DEBUG_LOCATION, "call_end_closure").release();
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_INIT(&batch_.handler_private.closure, CallEndedRetry, this,
                          grpc_schedule_on_exec_ctx),
        GRPC_ERROR_NONE);
    return;
  }
  // Initialize payload and batch.
  payload_.context = context_;
  batch_.payload = &payload_;
  // on_complete callback takes ref, handled manually.
  call_->Ref(DEBUG_LOCATION, "on_complete").release();
  batch_.on_complete = GRPC_CLOSURE_INIT(&on_complete_, OnComplete, this,
                                         grpc_schedule_on_exec_ctx);
  // Add send_initial_metadata op.
  grpc_metadata_batch_init(&send_initial_metadata_);
  error = grpc_metadata_batch_add_head(
      &send_initial_metadata_, &path_metadata_storage_,
      grpc_mdelem_from_slices(
          GRPC_MDSTR_PATH,
          GRPC_MDSTR_SLASH_GRPC_DOT_HEALTH_DOT_V1_DOT_HEALTH_SLASH_WATCH),
      GRPC_BATCH_PATH);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  payload_.send_initial_metadata.send_initial_metadata =
      &send_initial_metadata_;
  payload_.send_initial_metadata.send_initial_metadata_flags = 0;
  payload_.send_initial_metadata.peer_string = nullptr;
  batch_.send_initial_metadata = true;
  // Add send_message op.
  EncodeRequest(health_check_client_->service_name_, &send_message_);
  payload_.send_message.send_message.reset(send_message_.get());
  batch_.send_message = true;
  // Add send_trailing_metadata op.
  grpc_metadata_batch_init(&send_trailing_metadata_);
  payload_.send_trailing_metadata.send_trailing_metadata =
      &send_trailing_metadata_;
  batch_.send_trailing_metadata = true;
  // Add recv_initial_metadata op.
  grpc_metadata_batch_init(&recv_initial_metadata_);
  payload_.recv_initial_metadata.recv_initial_metadata =
      &recv_initial_metadata_;
  payload_.recv_initial_metadata.recv_flags = nullptr;
  payload_.recv_initial_metadata.trailing_metadata_available = nullptr;
  payload_.recv_initial_metadata.peer_string = nullptr;
  // recv_initial_metadata_ready callback takes ref, handled manually.
  call_->Ref(DEBUG_LOCATION, "recv_initial_metadata_ready").release();
  payload_.recv_initial_metadata.recv_initial_metadata_ready =
      GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                        this, grpc_schedule_on_exec_ctx);
  batch_.recv_initial_metadata = true;
  // Add recv_message op.
  payload_.recv_message.recv_message = &recv_message_;
  // recv_message callback takes ref, handled manually.
  call_->Ref(DEBUG_LOCATION, "recv_message_ready").release();
  payload_.recv_message.recv_message_ready = GRPC_CLOSURE_INIT(
      &recv_message_ready_, RecvMessageReady, this, grpc_schedule_on_exec_ctx);
  batch_.recv_message = true;
  // Start batch.
  StartBatch(&batch_);
  // Initialize recv_trailing_metadata batch.
  recv_trailing_metadata_batch_.payload = &payload_;
  // Add recv_trailing_metadata op.
  grpc_metadata_batch_init(&recv_trailing_metadata_);
  payload_.recv_trailing_metadata.recv_trailing_metadata =
      &recv_trailing_metadata_;
  payload_.recv_trailing_metadata.collect_stats = &collect_stats_;
  // This callback signals the end of the call, so it relies on the
  // initial ref instead of taking a new ref.  When it's invoked, the
  // initial ref is released.
  payload_.recv_trailing_metadata.recv_trailing_metadata_ready =
      GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                        RecvTrailingMetadataReady, this,
                        grpc_schedule_on_exec_ctx);
  recv_trailing_metadata_batch_.recv_trailing_metadata = true;
  // Start recv_trailing_metadata batch.
  StartBatch(&recv_trailing_metadata_batch_);
}

void HealthCheckClient::CallState::StartBatchInCallCombiner(void* arg,
                                                            grpc_error* error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  SubchannelCall* call =
      static_cast<SubchannelCall*>(batch->handler_private.extra_arg);
  call->StartTransportStreamOpBatch(batch);
}

void HealthCheckClient::CallState::StartBatch(
    grpc_transport_stream_op_batch* batch) {
  batch->handler_private.extra_arg = call_;
  GRPC_CLOSURE_INIT(&batch->handler_private.closure, StartBatchInCallCombiner,
                    batch, grpc_schedule_on_exec_ctx);
  GRPC_CALL_COMBINER_START(&call_combiner_, &batch->handler_private.closure,
                           GRPC_ERROR_NONE, "start_subchannel_batch");
}

void HealthCheckClient::CallState::AfterCallStackDestruction(
    void* arg, grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  Delete(self);
}

void HealthCheckClient::CallState::OnCancelComplete(void* arg,
                                                    grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  GRPC_CALL_COMBINER_STOP(&self->call_combiner_, "health_cancel");
  self->call_->Unref(DEBUG_LOCATION, "cancel");
}

void HealthCheckClient::CallState::StartCancel(void* arg, grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  auto* batch = grpc_make_transport_stream_op(
      GRPC_CLOSURE_CREATE(OnCancelComplete, self, grpc_schedule_on_exec_ctx));
  batch->cancel_stream = true;
  batch->payload->cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
  self->call_->StartTransportStreamOpBatch(batch);
}

void HealthCheckClient::CallState::Cancel() {
  bool expected = false;
  if (cancelled_.CompareExchangeStrong(&expected, true, MemoryOrder::ACQ_REL,
                                       MemoryOrder::ACQUIRE)) {
    call_->Ref(DEBUG_LOCATION, "cancel").release();
    GRPC_CALL_COMBINER_START(
        &call_combiner_,
        GRPC_CLOSURE_CREATE(StartCancel, this, grpc_schedule_on_exec_ctx),
        GRPC_ERROR_NONE, "health_cancel");
  }
}

void HealthCheckClient::CallState::OnComplete(void* arg, grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  GRPC_CALL_COMBINER_STOP(&self->call_combiner_, "on_complete");
  grpc_metadata_batch_destroy(&self->send_initial_metadata_);
  grpc_metadata_batch_destroy(&self->send_trailing_metadata_);
  self->call_->Unref(DEBUG_LOCATION, "on_complete");
}

void HealthCheckClient::CallState::RecvInitialMetadataReady(void* arg,
                                                            grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  GRPC_CALL_COMBINER_STOP(&self->call_combiner_, "recv_initial_metadata_ready");
  grpc_metadata_batch_destroy(&self->recv_initial_metadata_);
  self->call_->Unref(DEBUG_LOCATION, "recv_initial_metadata_ready");
}

void HealthCheckClient::CallState::DoneReadingRecvMessage(grpc_error* error) {
  recv_message_.reset();
  if (error != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(error);
    Cancel();
    grpc_slice_buffer_destroy_internal(&recv_message_buffer_);
    call_->Unref(DEBUG_LOCATION, "recv_message_ready");
    return;
  }
  const bool healthy = DecodeResponse(&recv_message_buffer_, &error);
  const grpc_connectivity_state state =
      healthy ? GRPC_CHANNEL_READY : GRPC_CHANNEL_TRANSIENT_FAILURE;
  if (error == GRPC_ERROR_NONE && !healthy) {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("backend unhealthy");
  }
  health_check_client_->SetHealthStatus(state, error);
  seen_response_.Store(true, MemoryOrder::RELEASE);
  grpc_slice_buffer_destroy_internal(&recv_message_buffer_);
  // Start another recv_message batch.
  // This re-uses the ref we're holding.
  // Note: Can't just reuse batch_ here, since we don't know that all
  // callbacks from the original batch have completed yet.
  recv_message_batch_.payload = &payload_;
  payload_.recv_message.recv_message = &recv_message_;
  payload_.recv_message.recv_message_ready = GRPC_CLOSURE_INIT(
      &recv_message_ready_, RecvMessageReady, this, grpc_schedule_on_exec_ctx);
  recv_message_batch_.recv_message = true;
  StartBatch(&recv_message_batch_);
}

grpc_error* HealthCheckClient::CallState::PullSliceFromRecvMessage() {
  grpc_slice slice;
  grpc_error* error = recv_message_->Pull(&slice);
  if (error == GRPC_ERROR_NONE) {
    grpc_slice_buffer_add(&recv_message_buffer_, slice);
  }
  return error;
}

void HealthCheckClient::CallState::ContinueReadingRecvMessage() {
  while (recv_message_->Next(SIZE_MAX, &recv_message_ready_)) {
    grpc_error* error = PullSliceFromRecvMessage();
    if (error != GRPC_ERROR_NONE) {
      DoneReadingRecvMessage(error);
      return;
    }
    if (recv_message_buffer_.length == recv_message_->length()) {
      DoneReadingRecvMessage(GRPC_ERROR_NONE);
      break;
    }
  }
}

void HealthCheckClient::CallState::OnByteStreamNext(void* arg,
                                                    grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  if (error != GRPC_ERROR_NONE) {
    self->DoneReadingRecvMessage(GRPC_ERROR_REF(error));
    return;
  }
  error = self->PullSliceFromRecvMessage();
  if (error != GRPC_ERROR_NONE) {
    self->DoneReadingRecvMessage(error);
    return;
  }
  if (self->recv_message_buffer_.length == self->recv_message_->length()) {
    self->DoneReadingRecvMessage(GRPC_ERROR_NONE);
  } else {
    self->ContinueReadingRecvMessage();
  }
}

void HealthCheckClient::CallState::RecvMessageReady(void* arg,
                                                    grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  GRPC_CALL_COMBINER_STOP(&self->call_combiner_, "recv_message_ready");
  if (self->recv_message_ == nullptr) {
    self->call_->Unref(DEBUG_LOCATION, "recv_message_ready");
    return;
  }
  grpc_slice_buffer_init(&self->recv_message_buffer_);
  GRPC_CLOSURE_INIT(&self->recv_message_ready_, OnByteStreamNext, self,
                    grpc_schedule_on_exec_ctx);
  self->ContinueReadingRecvMessage();
  // Ref will continue to be held until we finish draining the byte stream.
}

void HealthCheckClient::CallState::RecvTrailingMetadataReady(
    void* arg, grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  GRPC_CALL_COMBINER_STOP(&self->call_combiner_,
                          "recv_trailing_metadata_ready");
  // Get call status.
  grpc_status_code status = GRPC_STATUS_UNKNOWN;
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, GRPC_MILLIS_INF_FUTURE, &status,
                          nullptr /* slice */, nullptr /* http_error */,
                          nullptr /* error_string */);
  } else if (self->recv_trailing_metadata_.idx.named.grpc_status != nullptr) {
    status = grpc_get_status_code_from_metadata(
        self->recv_trailing_metadata_.idx.named.grpc_status->md);
  }
  if (GRPC_TRACE_FLAG_ENABLED(grpc_health_check_client_trace)) {
    gpr_log(GPR_INFO,
            "HealthCheckClient %p CallState %p: health watch failed with "
            "status %d",
            self->health_check_client_.get(), self, status);
  }
  // Clean up.
  grpc_metadata_batch_destroy(&self->recv_trailing_metadata_);
  // For status UNIMPLEMENTED, give up and assume always healthy.
  bool retry = true;
  if (status == GRPC_STATUS_UNIMPLEMENTED) {
    static const char kErrorMessage[] =
        "health checking Watch method returned UNIMPLEMENTED; "
        "disabling health checks but assuming server is healthy";
    gpr_log(GPR_ERROR, kErrorMessage);
    if (self->health_check_client_->channelz_node_ != nullptr) {
      self->health_check_client_->channelz_node_->AddTraceEvent(
          channelz::ChannelTrace::Error,
          grpc_slice_from_static_string(kErrorMessage));
    }
    self->health_check_client_->SetHealthStatus(GRPC_CHANNEL_READY,
                                                GRPC_ERROR_NONE);
    retry = false;
  }
  self->CallEnded(retry);
}

void HealthCheckClient::CallState::CallEndedRetry(void* arg,
                                                  grpc_error* error) {
  HealthCheckClient::CallState* self =
      static_cast<HealthCheckClient::CallState*>(arg);
  self->CallEnded(true /* retry */);
  self->call_->Unref(DEBUG_LOCATION, "call_end_closure");
}

void HealthCheckClient::CallState::CallEnded(bool retry) {
  // If this CallState is still in use, this call ended because of a failure,
  // so we need to stop using it and optionally create a new one.
  // Otherwise, we have deliberately ended this call, and no further action
  // is required.
  if (this == health_check_client_->call_state_.get()) {
    health_check_client_->call_state_.reset();
    if (retry) {
      GPR_ASSERT(!health_check_client_->shutting_down_);
      if (seen_response_.Load(MemoryOrder::ACQUIRE)) {
        // If the call fails after we've gotten a successful response, reset
        // the backoff and restart the call immediately.
        health_check_client_->retry_backoff_.Reset();
        health_check_client_->StartCall();
      } else {
        // If the call failed without receiving any messages, retry later.
        health_check_client_->StartRetryTimer();
      }
    }
  }
  // When the last ref to the call stack goes away, the CallState object
  // will be automatically destroyed.
  call_->Unref(DEBUG_LOCATION, "call_ended");
}

}  // namespace grpc_core
