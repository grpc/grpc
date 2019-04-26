/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/filters/client_channel/subchannel.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <cstring>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/health/health_check_client.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/service_config.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/manual_constructor.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/status_metadata.h"
#include "src/core/lib/uri/uri_parser.h"

// Strong and weak refs.
#define INTERNAL_REF_BITS 16
#define STRONG_REF_MASK (~(gpr_atm)((1 << INTERNAL_REF_BITS) - 1))

// Backoff parameters.
#define GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_SUBCHANNEL_RECONNECT_MIN_TIMEOUT_SECONDS 20
#define GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_SUBCHANNEL_RECONNECT_JITTER 0.2

// Conversion between subchannel call and call stack.
#define SUBCHANNEL_CALL_TO_CALL_STACK(call) \
  (grpc_call_stack*)((char*)(call) +        \
                     GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)))
#define CALL_STACK_TO_SUBCHANNEL_CALL(callstack) \
  (SubchannelCall*)(((char*)(call_stack)) -      \
                    GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)))

namespace grpc_core {

//
// ConnectedSubchannel
//

ConnectedSubchannel::ConnectedSubchannel(
    grpc_channel_stack* channel_stack, const grpc_channel_args* args,
    RefCountedPtr<channelz::SubchannelNode> channelz_subchannel,
    intptr_t socket_uuid)
    : RefCounted<ConnectedSubchannel>(&grpc_trace_stream_refcount),
      channel_stack_(channel_stack),
      args_(grpc_channel_args_copy(args)),
      channelz_subchannel_(std::move(channelz_subchannel)),
      socket_uuid_(socket_uuid) {}

ConnectedSubchannel::~ConnectedSubchannel() {
  grpc_channel_args_destroy(args_);
  GRPC_CHANNEL_STACK_UNREF(channel_stack_, "connected_subchannel_dtor");
}

void ConnectedSubchannel::NotifyOnStateChange(
    grpc_pollset_set* interested_parties, grpc_connectivity_state* state,
    grpc_closure* closure) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  op->connectivity_state = state;
  op->on_connectivity_state_change = closure;
  op->bind_pollset_set = interested_parties;
  elem = grpc_channel_stack_element(channel_stack_, 0);
  elem->filter->start_transport_op(elem, op);
}

void ConnectedSubchannel::Ping(grpc_closure* on_initiate,
                               grpc_closure* on_ack) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  grpc_channel_element* elem;
  op->send_ping.on_initiate = on_initiate;
  op->send_ping.on_ack = on_ack;
  elem = grpc_channel_stack_element(channel_stack_, 0);
  elem->filter->start_transport_op(elem, op);
}

RefCountedPtr<SubchannelCall> ConnectedSubchannel::CreateCall(
    const CallArgs& args, grpc_error** error) {
  const size_t allocation_size =
      GetInitialCallSizeEstimate(args.parent_data_size);
  RefCountedPtr<SubchannelCall> call(
      new (args.arena->Alloc(allocation_size))
          SubchannelCall(Ref(DEBUG_LOCATION, "subchannel_call"), args));
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(call.get());
  const grpc_call_element_args call_args = {
      callstk,           /* call_stack */
      nullptr,           /* server_transport_data */
      args.context,      /* context */
      args.path,         /* path */
      args.start_time,   /* start_time */
      args.deadline,     /* deadline */
      args.arena,        /* arena */
      args.call_combiner /* call_combiner */
  };
  *error = grpc_call_stack_init(channel_stack_, 1, SubchannelCall::Destroy,
                                call.get(), &call_args);
  if (GPR_UNLIKELY(*error != GRPC_ERROR_NONE)) {
    const char* error_string = grpc_error_string(*error);
    gpr_log(GPR_ERROR, "error: %s", error_string);
    return call;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args.pollent);
  if (channelz_subchannel_ != nullptr) {
    channelz_subchannel_->RecordCallStarted();
  }
  return call;
}

size_t ConnectedSubchannel::GetInitialCallSizeEstimate(
    size_t parent_data_size) const {
  size_t allocation_size =
      GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall));
  if (parent_data_size > 0) {
    allocation_size +=
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(channel_stack_->call_stack_size) +
        parent_data_size;
  } else {
    allocation_size += channel_stack_->call_stack_size;
  }
  return allocation_size;
}

//
// SubchannelCall
//

void SubchannelCall::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  GPR_TIMER_SCOPE("subchannel_call_process_op", 0);
  MaybeInterceptRecvTrailingMetadata(batch);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_CALL_LOG_OP(GPR_INFO, top_elem, batch);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

void* SubchannelCall::GetParentData() {
  grpc_channel_stack* chanstk = connected_subchannel_->channel_stack();
  return (char*)this + GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)) +
         GPR_ROUND_UP_TO_ALIGNMENT_SIZE(chanstk->call_stack_size);
}

grpc_call_stack* SubchannelCall::GetCallStack() {
  return SUBCHANNEL_CALL_TO_CALL_STACK(this);
}

void SubchannelCall::SetAfterCallStackDestroy(grpc_closure* closure) {
  GPR_ASSERT(after_call_stack_destroy_ == nullptr);
  GPR_ASSERT(closure != nullptr);
  after_call_stack_destroy_ = closure;
}

RefCountedPtr<SubchannelCall> SubchannelCall::Ref() {
  IncrementRefCount();
  return RefCountedPtr<SubchannelCall>(this);
}

RefCountedPtr<SubchannelCall> SubchannelCall::Ref(
    const grpc_core::DebugLocation& location, const char* reason) {
  IncrementRefCount(location, reason);
  return RefCountedPtr<SubchannelCall>(this);
}

void SubchannelCall::Unref() {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void SubchannelCall::Unref(const DebugLocation& location, const char* reason) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

void SubchannelCall::Destroy(void* arg, grpc_error* error) {
  GPR_TIMER_SCOPE("subchannel_call_destroy", 0);
  SubchannelCall* self = static_cast<SubchannelCall*>(arg);
  // Keep some members before destroying the subchannel call.
  grpc_closure* after_call_stack_destroy = self->after_call_stack_destroy_;
  RefCountedPtr<ConnectedSubchannel> connected_subchannel =
      std::move(self->connected_subchannel_);
  // Destroy the subchannel call.
  self->~SubchannelCall();
  // Destroy the call stack. This should be after destroying the subchannel
  // call, because call->after_call_stack_destroy(), if not null, will free the
  // call arena.
  grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(self), nullptr,
                          after_call_stack_destroy);
  // Automatically reset connected_subchannel. This should be after destroying
  // the call stack, because destroying call stack needs access to the channel
  // stack.
}

void SubchannelCall::MaybeInterceptRecvTrailingMetadata(
    grpc_transport_stream_op_batch* batch) {
  // only intercept payloads with recv trailing.
  if (!batch->recv_trailing_metadata) {
    return;
  }
  // only add interceptor is channelz is enabled.
  if (connected_subchannel_->channelz_subchannel() == nullptr) {
    return;
  }
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    this, grpc_schedule_on_exec_ctx);
  // save some state needed for the interception callback.
  GPR_ASSERT(recv_trailing_metadata_ == nullptr);
  recv_trailing_metadata_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata;
  original_recv_trailing_metadata_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_metadata_ready_;
}

namespace {

// Sets *status based on the rest of the parameters.
void GetCallStatus(grpc_status_code* status, grpc_millis deadline,
                   grpc_metadata_batch* md_batch, grpc_error* error) {
  if (error != GRPC_ERROR_NONE) {
    grpc_error_get_status(error, deadline, status, nullptr, nullptr, nullptr);
  } else {
    if (md_batch->idx.named.grpc_status != nullptr) {
      *status = grpc_get_status_code_from_metadata(
          md_batch->idx.named.grpc_status->md);
    } else {
      *status = GRPC_STATUS_UNKNOWN;
    }
  }
  GRPC_ERROR_UNREF(error);
}

}  // namespace

void SubchannelCall::RecvTrailingMetadataReady(void* arg, grpc_error* error) {
  SubchannelCall* call = static_cast<SubchannelCall*>(arg);
  GPR_ASSERT(call->recv_trailing_metadata_ != nullptr);
  grpc_status_code status = GRPC_STATUS_OK;
  GetCallStatus(&status, call->deadline_, call->recv_trailing_metadata_,
                GRPC_ERROR_REF(error));
  channelz::SubchannelNode* channelz_subchannel =
      call->connected_subchannel_->channelz_subchannel();
  GPR_ASSERT(channelz_subchannel != nullptr);
  if (status == GRPC_STATUS_OK) {
    channelz_subchannel->RecordCallSucceeded();
  } else {
    channelz_subchannel->RecordCallFailed();
  }
  GRPC_CLOSURE_RUN(call->original_recv_trailing_metadata_,
                   GRPC_ERROR_REF(error));
}

void SubchannelCall::IncrementRefCount() {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void SubchannelCall::IncrementRefCount(const grpc_core::DebugLocation& location,
                                       const char* reason) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

//
// Subchannel::ConnectedSubchannelStateWatcher
//

class Subchannel::ConnectedSubchannelStateWatcher
    : public InternallyRefCounted<ConnectedSubchannelStateWatcher> {
 public:
  // Must be instantiated while holding c->mu.
  explicit ConnectedSubchannelStateWatcher(Subchannel* c) : subchannel_(c) {
    // Steal subchannel ref for connecting.
    GRPC_SUBCHANNEL_WEAK_REF(subchannel_, "state_watcher");
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "connecting");
    // Start watching for connectivity state changes.
    // Callback uses initial ref to this.
    GRPC_CLOSURE_INIT(&on_connectivity_changed_, OnConnectivityChanged, this,
                      grpc_schedule_on_exec_ctx);
    c->connected_subchannel_->NotifyOnStateChange(c->pollset_set_,
                                                  &pending_connectivity_state_,
                                                  &on_connectivity_changed_);
    // Start health check if needed.
    grpc_connectivity_state health_state = GRPC_CHANNEL_READY;
    if (c->health_check_service_name_ != nullptr) {
      health_check_client_ = MakeOrphanable<HealthCheckClient>(
          c->health_check_service_name_.get(), c->connected_subchannel_,
          c->pollset_set_, c->channelz_node_);
      GRPC_CLOSURE_INIT(&on_health_changed_, OnHealthChanged, this,
                        grpc_schedule_on_exec_ctx);
      Ref().release();  // Ref for health callback tracked manually.
      health_check_client_->NotifyOnHealthChange(&health_state_,
                                                 &on_health_changed_);
      health_state = GRPC_CHANNEL_CONNECTING;
    }
    // Report initial state.
    c->SetConnectivityStateLocked(GRPC_CHANNEL_READY, "subchannel_connected");
    grpc_connectivity_state_set(&c->state_and_health_tracker_, health_state,
                                "subchannel_connected");
  }

  ~ConnectedSubchannelStateWatcher() {
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "state_watcher");
  }

  // Must be called while holding subchannel_->mu.
  void Orphan() override { health_check_client_.reset(); }

 private:
  static void OnConnectivityChanged(void* arg, grpc_error* error) {
    auto* self = static_cast<ConnectedSubchannelStateWatcher*>(arg);
    Subchannel* c = self->subchannel_;
    {
      MutexLock lock(&c->mu_);
      switch (self->pending_connectivity_state_) {
        case GRPC_CHANNEL_TRANSIENT_FAILURE:
        case GRPC_CHANNEL_SHUTDOWN: {
          if (!c->disconnected_ && c->connected_subchannel_ != nullptr) {
            if (grpc_trace_stream_refcount.enabled()) {
              gpr_log(GPR_INFO,
                      "Connected subchannel %p of subchannel %p has gone into "
                      "%s. Attempting to reconnect.",
                      c->connected_subchannel_.get(), c,
                      grpc_connectivity_state_name(
                          self->pending_connectivity_state_));
            }
            c->connected_subchannel_.reset();
            c->connected_subchannel_watcher_.reset();
            self->last_connectivity_state_ = GRPC_CHANNEL_TRANSIENT_FAILURE;
            c->SetConnectivityStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                          "reflect_child");
            grpc_connectivity_state_set(&c->state_and_health_tracker_,
                                        GRPC_CHANNEL_TRANSIENT_FAILURE,
                                        "reflect_child");
            c->backoff_begun_ = false;
            c->backoff_.Reset();
            c->MaybeStartConnectingLocked();
          } else {
            self->last_connectivity_state_ = GRPC_CHANNEL_SHUTDOWN;
          }
          self->health_check_client_.reset();
          break;
        }
        default: {
          // In principle, this should never happen.  We should not get
          // a callback for READY, because that was the state we started
          // this watch from.  And a connected subchannel should never go
          // from READY to CONNECTING or IDLE.
          self->last_connectivity_state_ = self->pending_connectivity_state_;
          c->SetConnectivityStateLocked(self->pending_connectivity_state_,
                                        "reflect_child");
          if (self->pending_connectivity_state_ != GRPC_CHANNEL_READY) {
            grpc_connectivity_state_set(&c->state_and_health_tracker_,
                                        self->pending_connectivity_state_,
                                        "reflect_child");
          }
          c->connected_subchannel_->NotifyOnStateChange(
              nullptr, &self->pending_connectivity_state_,
              &self->on_connectivity_changed_);
          self = nullptr;  // So we don't unref below.
        }
      }
    }
    // Don't unref until we've released the lock, because this might
    // cause the subchannel (which contains the lock) to be destroyed.
    if (self != nullptr) self->Unref();
  }

  static void OnHealthChanged(void* arg, grpc_error* error) {
    auto* self = static_cast<ConnectedSubchannelStateWatcher*>(arg);
    Subchannel* c = self->subchannel_;
    {
      MutexLock lock(&c->mu_);
      if (self->health_state_ != GRPC_CHANNEL_SHUTDOWN &&
          self->health_check_client_ != nullptr) {
        if (self->last_connectivity_state_ == GRPC_CHANNEL_READY) {
          grpc_connectivity_state_set(&c->state_and_health_tracker_,
                                      self->health_state_, "health_changed");
        }
        self->health_check_client_->NotifyOnHealthChange(
            &self->health_state_, &self->on_health_changed_);
        self = nullptr;  // So we don't unref below.
      }
    }
    // Don't unref until we've released the lock, because this might
    // cause the subchannel (which contains the lock) to be destroyed.
    if (self != nullptr) self->Unref();
  }

  Subchannel* subchannel_;
  grpc_closure on_connectivity_changed_;
  grpc_connectivity_state pending_connectivity_state_ = GRPC_CHANNEL_READY;
  grpc_connectivity_state last_connectivity_state_ = GRPC_CHANNEL_READY;
  OrphanablePtr<HealthCheckClient> health_check_client_;
  grpc_closure on_health_changed_;
  grpc_connectivity_state health_state_ = GRPC_CHANNEL_CONNECTING;
};

//
// Subchannel::ExternalStateWatcher
//

struct Subchannel::ExternalStateWatcher {
  ExternalStateWatcher(Subchannel* subchannel, grpc_pollset_set* pollset_set,
                       grpc_closure* notify)
      : subchannel(subchannel), pollset_set(pollset_set), notify(notify) {
    GRPC_SUBCHANNEL_WEAK_REF(subchannel, "external_state_watcher+init");
    GRPC_CLOSURE_INIT(&on_state_changed, OnStateChanged, this,
                      grpc_schedule_on_exec_ctx);
  }

  static void OnStateChanged(void* arg, grpc_error* error) {
    ExternalStateWatcher* w = static_cast<ExternalStateWatcher*>(arg);
    grpc_closure* follow_up = w->notify;
    if (w->pollset_set != nullptr) {
      grpc_pollset_set_del_pollset_set(w->subchannel->pollset_set_,
                                       w->pollset_set);
    }
    {
      MutexLock lock(&w->subchannel->mu_);
      if (w->subchannel->external_state_watcher_list_ == w) {
        w->subchannel->external_state_watcher_list_ = w->next;
      }
      if (w->next != nullptr) w->next->prev = w->prev;
      if (w->prev != nullptr) w->prev->next = w->next;
    }
    GRPC_SUBCHANNEL_WEAK_UNREF(w->subchannel, "external_state_watcher+done");
    Delete(w);
    GRPC_CLOSURE_SCHED(follow_up, GRPC_ERROR_REF(error));
  }

  Subchannel* subchannel;
  grpc_pollset_set* pollset_set;
  grpc_closure* notify;
  grpc_closure on_state_changed;
  ExternalStateWatcher* next = nullptr;
  ExternalStateWatcher* prev = nullptr;
};

//
// Subchannel
//

namespace {

BackOff::Options ParseArgsForBackoffValues(
    const grpc_channel_args* args, grpc_millis* min_connect_timeout_ms) {
  grpc_millis initial_backoff_ms =
      GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS * 1000;
  *min_connect_timeout_ms =
      GRPC_SUBCHANNEL_RECONNECT_MIN_TIMEOUT_SECONDS * 1000;
  grpc_millis max_backoff_ms =
      GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS * 1000;
  bool fixed_reconnect_backoff = false;
  if (args != nullptr) {
    for (size_t i = 0; i < args->num_args; i++) {
      if (0 == strcmp(args->args[i].key,
                      "grpc.testing.fixed_reconnect_backoff_ms")) {
        fixed_reconnect_backoff = true;
        initial_backoff_ms = *min_connect_timeout_ms = max_backoff_ms =
            grpc_channel_arg_get_integer(
                &args->args[i],
                {static_cast<int>(initial_backoff_ms), 100, INT_MAX});
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_MIN_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        *min_connect_timeout_ms = grpc_channel_arg_get_integer(
            &args->args[i],
            {static_cast<int>(*min_connect_timeout_ms), 100, INT_MAX});
      } else if (0 ==
                 strcmp(args->args[i].key, GRPC_ARG_MAX_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        max_backoff_ms = grpc_channel_arg_get_integer(
            &args->args[i], {static_cast<int>(max_backoff_ms), 100, INT_MAX});
      } else if (0 == strcmp(args->args[i].key,
                             GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS)) {
        fixed_reconnect_backoff = false;
        initial_backoff_ms = grpc_channel_arg_get_integer(
            &args->args[i],
            {static_cast<int>(initial_backoff_ms), 100, INT_MAX});
      }
    }
  }
  return BackOff::Options()
      .set_initial_backoff(initial_backoff_ms)
      .set_multiplier(fixed_reconnect_backoff
                          ? 1.0
                          : GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER)
      .set_jitter(fixed_reconnect_backoff ? 0.0
                                          : GRPC_SUBCHANNEL_RECONNECT_JITTER)
      .set_max_backoff(max_backoff_ms);
}

}  // namespace

Subchannel::Subchannel(SubchannelKey* key, grpc_connector* connector,
                       const grpc_channel_args* args)
    : key_(key),
      connector_(connector),
      backoff_(ParseArgsForBackoffValues(args, &min_connect_timeout_ms_)) {
  GRPC_STATS_INC_CLIENT_SUBCHANNELS_CREATED();
  gpr_atm_no_barrier_store(&ref_pair_, 1 << INTERNAL_REF_BITS);
  grpc_connector_ref(connector_);
  pollset_set_ = grpc_pollset_set_create();
  grpc_resolved_address* addr =
      static_cast<grpc_resolved_address*>(gpr_malloc(sizeof(*addr)));
  GetAddressFromSubchannelAddressArg(args, addr);
  grpc_resolved_address* new_address = nullptr;
  grpc_channel_args* new_args = nullptr;
  if (grpc_proxy_mappers_map_address(addr, args, &new_address, &new_args)) {
    GPR_ASSERT(new_address != nullptr);
    gpr_free(addr);
    addr = new_address;
  }
  static const char* keys_to_remove[] = {GRPC_ARG_SUBCHANNEL_ADDRESS};
  grpc_arg new_arg = CreateSubchannelAddressArg(addr);
  gpr_free(addr);
  args_ = grpc_channel_args_copy_and_add_and_remove(
      new_args != nullptr ? new_args : args, keys_to_remove,
      GPR_ARRAY_SIZE(keys_to_remove), &new_arg, 1);
  gpr_free(new_arg.value.string);
  if (new_args != nullptr) grpc_channel_args_destroy(new_args);
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished, this,
                    grpc_schedule_on_exec_ctx);
  grpc_connectivity_state_init(&state_tracker_, GRPC_CHANNEL_IDLE,
                               "subchannel");
  grpc_connectivity_state_init(&state_and_health_tracker_, GRPC_CHANNEL_IDLE,
                               "subchannel");
  health_check_service_name_ =
      UniquePtr<char>(gpr_strdup(grpc_channel_arg_get_string(
          grpc_channel_args_find(args_, "grpc.temp.health_check"))));
  const grpc_arg* arg = grpc_channel_args_find(args_, GRPC_ARG_ENABLE_CHANNELZ);
  const bool channelz_enabled =
      grpc_channel_arg_get_bool(arg, GRPC_ENABLE_CHANNELZ_DEFAULT);
  arg = grpc_channel_args_find(
      args_, GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE);
  const grpc_integer_options options = {
      GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT, 0, INT_MAX};
  size_t channel_tracer_max_memory =
      (size_t)grpc_channel_arg_get_integer(arg, options);
  if (channelz_enabled) {
    channelz_node_ = MakeRefCounted<channelz::SubchannelNode>(
        this, channel_tracer_max_memory);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("subchannel created"));
  }
}

Subchannel::~Subchannel() {
  if (channelz_node_ != nullptr) {
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string("Subchannel destroyed"));
    channelz_node_->MarkSubchannelDestroyed();
  }
  grpc_channel_args_destroy(args_);
  grpc_connectivity_state_destroy(&state_tracker_);
  grpc_connectivity_state_destroy(&state_and_health_tracker_);
  grpc_connector_unref(connector_);
  grpc_pollset_set_destroy(pollset_set_);
  Delete(key_);
}

Subchannel* Subchannel::Create(grpc_connector* connector,
                               const grpc_channel_args* args) {
  SubchannelKey* key = New<SubchannelKey>(args);
  SubchannelPoolInterface* subchannel_pool =
      SubchannelPoolInterface::GetSubchannelPoolFromChannelArgs(args);
  GPR_ASSERT(subchannel_pool != nullptr);
  Subchannel* c = subchannel_pool->FindSubchannel(key);
  if (c != nullptr) {
    Delete(key);
    return c;
  }
  c = New<Subchannel>(key, connector, args);
  // Try to register the subchannel before setting the subchannel pool.
  // Otherwise, in case of a registration race, unreffing c in
  // RegisterSubchannel() will cause c to be tried to be unregistered, while
  // its key maps to a different subchannel.
  Subchannel* registered = subchannel_pool->RegisterSubchannel(key, c);
  if (registered == c) c->subchannel_pool_ = subchannel_pool->Ref();
  return registered;
}

Subchannel* Subchannel::Ref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = RefMutate((1 << INTERNAL_REF_BITS),
                       0 GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE("STRONG_REF"));
  GPR_ASSERT((old_refs & STRONG_REF_MASK) != 0);
  return this;
}

void Subchannel::Unref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  // add a weak ref and subtract a strong ref (atomically)
  old_refs = RefMutate(
      static_cast<gpr_atm>(1) - static_cast<gpr_atm>(1 << INTERNAL_REF_BITS),
      1 GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE("STRONG_UNREF"));
  if ((old_refs & STRONG_REF_MASK) == (1 << INTERNAL_REF_BITS)) {
    Disconnect();
  }
  GRPC_SUBCHANNEL_WEAK_UNREF(this, "strong-unref");
}

Subchannel* Subchannel::WeakRef(GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = RefMutate(1, 0 GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE("WEAK_REF"));
  GPR_ASSERT(old_refs != 0);
  return this;
}

namespace {

void subchannel_destroy(void* arg, grpc_error* error) {
  Subchannel* self = static_cast<Subchannel*>(arg);
  Delete(self);
}

}  // namespace

void Subchannel::WeakUnref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = RefMutate(-static_cast<gpr_atm>(1),
                       1 GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE("WEAK_UNREF"));
  if (old_refs == 1) {
    GRPC_CLOSURE_SCHED(GRPC_CLOSURE_CREATE(subchannel_destroy, this,
                                           grpc_schedule_on_exec_ctx),
                       GRPC_ERROR_NONE);
  }
}

Subchannel* Subchannel::RefFromWeakRef(GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  for (;;) {
    gpr_atm old_refs = gpr_atm_acq_load(&ref_pair_);
    if (old_refs >= (1 << INTERNAL_REF_BITS)) {
      gpr_atm new_refs = old_refs + (1 << INTERNAL_REF_BITS);
      if (gpr_atm_rel_cas(&ref_pair_, old_refs, new_refs)) {
        return this;
      }
    } else {
      return nullptr;
    }
  }
}

intptr_t Subchannel::GetChildSocketUuid() {
  if (connected_subchannel_ != nullptr) {
    return connected_subchannel_->socket_uuid();
  } else {
    return 0;
  }
}

const char* Subchannel::GetTargetAddress() {
  const grpc_arg* addr_arg =
      grpc_channel_args_find(args_, GRPC_ARG_SUBCHANNEL_ADDRESS);
  const char* addr_str = grpc_channel_arg_get_string(addr_arg);
  GPR_ASSERT(addr_str != nullptr);  // Should have been set by LB policy.
  return addr_str;
}

RefCountedPtr<ConnectedSubchannel> Subchannel::connected_subchannel() {
  MutexLock lock(&mu_);
  return connected_subchannel_;
}

channelz::SubchannelNode* Subchannel::channelz_node() {
  return channelz_node_.get();
}

grpc_connectivity_state Subchannel::CheckConnectivity(
    bool inhibit_health_checking) {
  grpc_connectivity_state_tracker* tracker =
      inhibit_health_checking ? &state_tracker_ : &state_and_health_tracker_;
  grpc_connectivity_state state = grpc_connectivity_state_check(tracker);
  return state;
}

void Subchannel::NotifyOnStateChange(grpc_pollset_set* interested_parties,
                                     grpc_connectivity_state* state,
                                     grpc_closure* notify,
                                     bool inhibit_health_checking) {
  grpc_connectivity_state_tracker* tracker =
      inhibit_health_checking ? &state_tracker_ : &state_and_health_tracker_;
  ExternalStateWatcher* w;
  if (state == nullptr) {
    MutexLock lock(&mu_);
    for (w = external_state_watcher_list_; w != nullptr; w = w->next) {
      if (w->notify == notify) {
        grpc_connectivity_state_notify_on_state_change(tracker, nullptr,
                                                       &w->on_state_changed);
      }
    }
  } else {
    w = New<ExternalStateWatcher>(this, interested_parties, notify);
    if (interested_parties != nullptr) {
      grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
    }
    MutexLock lock(&mu_);
    if (external_state_watcher_list_ != nullptr) {
      w->next = external_state_watcher_list_;
      w->next->prev = w;
    }
    external_state_watcher_list_ = w;
    grpc_connectivity_state_notify_on_state_change(tracker, state,
                                                   &w->on_state_changed);
    MaybeStartConnectingLocked();
  }
}

void Subchannel::ResetBackoff() {
  MutexLock lock(&mu_);
  backoff_.Reset();
  if (have_retry_alarm_) {
    retry_immediately_ = true;
    grpc_timer_cancel(&retry_alarm_);
  } else {
    backoff_begun_ = false;
    MaybeStartConnectingLocked();
  }
}

grpc_arg Subchannel::CreateSubchannelAddressArg(
    const grpc_resolved_address* addr) {
  return grpc_channel_arg_string_create(
      (char*)GRPC_ARG_SUBCHANNEL_ADDRESS,
      addr->len > 0 ? grpc_sockaddr_to_uri(addr) : gpr_strdup(""));
}

const char* Subchannel::GetUriFromSubchannelAddressArg(
    const grpc_channel_args* args) {
  const grpc_arg* addr_arg =
      grpc_channel_args_find(args, GRPC_ARG_SUBCHANNEL_ADDRESS);
  const char* addr_str = grpc_channel_arg_get_string(addr_arg);
  GPR_ASSERT(addr_str != nullptr);  // Should have been set by LB policy.
  return addr_str;
}

namespace {

void UriToSockaddr(const char* uri_str, grpc_resolved_address* addr) {
  grpc_uri* uri = grpc_uri_parse(uri_str, 0 /* suppress_errors */);
  GPR_ASSERT(uri != nullptr);
  if (!grpc_parse_uri(uri, addr)) memset(addr, 0, sizeof(*addr));
  grpc_uri_destroy(uri);
}

}  // namespace

void Subchannel::GetAddressFromSubchannelAddressArg(
    const grpc_channel_args* args, grpc_resolved_address* addr) {
  const char* addr_uri_str = GetUriFromSubchannelAddressArg(args);
  memset(addr, 0, sizeof(*addr));
  if (*addr_uri_str != '\0') {
    UriToSockaddr(addr_uri_str, addr);
  }
}

namespace {

// Returns a string indicating the subchannel's connectivity state change to
// \a state.
const char* SubchannelConnectivityStateChangeString(
    grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_IDLE:
      return "Subchannel state change to IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "Subchannel state change to CONNECTING";
    case GRPC_CHANNEL_READY:
      return "Subchannel state change to READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "Subchannel state change to TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "Subchannel state change to SHUTDOWN";
  }
  GPR_UNREACHABLE_CODE(return "UNKNOWN");
}

}  // namespace

void Subchannel::SetConnectivityStateLocked(grpc_connectivity_state state,
                                            const char* reason) {
  if (channelz_node_ != nullptr) {
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            SubchannelConnectivityStateChangeString(state)));
  }
  grpc_connectivity_state_set(&state_tracker_, state, reason);
}

void Subchannel::MaybeStartConnectingLocked() {
  if (disconnected_) {
    // Don't try to connect if we're already disconnected.
    return;
  }
  if (connecting_) {
    // Already connecting: don't restart.
    return;
  }
  if (connected_subchannel_ != nullptr) {
    // Already connected: don't restart.
    return;
  }
  if (!grpc_connectivity_state_has_watchers(&state_tracker_) &&
      !grpc_connectivity_state_has_watchers(&state_and_health_tracker_)) {
    // Nobody is interested in connecting: so don't just yet.
    return;
  }
  connecting_ = true;
  GRPC_SUBCHANNEL_WEAK_REF(this, "connecting");
  if (!backoff_begun_) {
    backoff_begun_ = true;
    ContinueConnectingLocked();
  } else {
    GPR_ASSERT(!have_retry_alarm_);
    have_retry_alarm_ = true;
    const grpc_millis time_til_next =
        next_attempt_deadline_ - ExecCtx::Get()->Now();
    if (time_til_next <= 0) {
      gpr_log(GPR_INFO, "Subchannel %p: Retry immediately", this);
    } else {
      gpr_log(GPR_INFO, "Subchannel %p: Retry in %" PRId64 " milliseconds",
              this, time_til_next);
    }
    GRPC_CLOSURE_INIT(&on_retry_alarm_, OnRetryAlarm, this,
                      grpc_schedule_on_exec_ctx);
    grpc_timer_init(&retry_alarm_, next_attempt_deadline_, &on_retry_alarm_);
  }
}

void Subchannel::OnRetryAlarm(void* arg, grpc_error* error) {
  Subchannel* c = static_cast<Subchannel*>(arg);
  // TODO(soheilhy): Once subchannel refcounting is simplified, we can get use
  //                 MutexLock instead of ReleasableMutexLock, here.
  ReleasableMutexLock lock(&c->mu_);
  c->have_retry_alarm_ = false;
  if (c->disconnected_) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING("Disconnected",
                                                             &error, 1);
  } else if (c->retry_immediately_) {
    c->retry_immediately_ = false;
    error = GRPC_ERROR_NONE;
  } else {
    GRPC_ERROR_REF(error);
  }
  if (error == GRPC_ERROR_NONE) {
    gpr_log(GPR_INFO, "Failed to connect to channel, retrying");
    c->ContinueConnectingLocked();
    lock.Unlock();
  } else {
    lock.Unlock();
    GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
  }
  GRPC_ERROR_UNREF(error);
}

void Subchannel::ContinueConnectingLocked() {
  grpc_connect_in_args args;
  args.interested_parties = pollset_set_;
  const grpc_millis min_deadline =
      min_connect_timeout_ms_ + ExecCtx::Get()->Now();
  next_attempt_deadline_ = backoff_.NextAttemptTime();
  args.deadline = std::max(next_attempt_deadline_, min_deadline);
  args.channel_args = args_;
  SetConnectivityStateLocked(GRPC_CHANNEL_CONNECTING, "connecting");
  grpc_connectivity_state_set(&state_and_health_tracker_,
                              GRPC_CHANNEL_CONNECTING, "connecting");
  grpc_connector_connect(connector_, &args, &connecting_result_,
                         &on_connecting_finished_);
}

void Subchannel::OnConnectingFinished(void* arg, grpc_error* error) {
  auto* c = static_cast<Subchannel*>(arg);
  grpc_channel_args* delete_channel_args = c->connecting_result_.channel_args;
  GRPC_SUBCHANNEL_WEAK_REF(c, "on_connecting_finished");
  {
    MutexLock lock(&c->mu_);
    c->connecting_ = false;
    if (c->connecting_result_.transport != nullptr &&
        c->PublishTransportLocked()) {
      // Do nothing, transport was published.
    } else if (c->disconnected_) {
      GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
    } else {
      gpr_log(GPR_INFO, "Connect failed: %s", grpc_error_string(error));
      c->SetConnectivityStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE,
                                    "connect_failed");
      grpc_connectivity_state_set(&c->state_and_health_tracker_,
                                  GRPC_CHANNEL_TRANSIENT_FAILURE,
                                  "connect_failed");
      c->MaybeStartConnectingLocked();
      GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
    }
  }
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "on_connecting_finished");
  grpc_channel_args_destroy(delete_channel_args);
}

namespace {

void ConnectionDestroy(void* arg, grpc_error* error) {
  grpc_channel_stack* stk = static_cast<grpc_channel_stack*>(arg);
  grpc_channel_stack_destroy(stk);
  gpr_free(stk);
}

}  // namespace

bool Subchannel::PublishTransportLocked() {
  // Construct channel stack.
  grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
  grpc_channel_stack_builder_set_channel_arguments(
      builder, connecting_result_.channel_args);
  grpc_channel_stack_builder_set_transport(builder,
                                           connecting_result_.transport);
  if (!grpc_channel_init_create_stack(builder, GRPC_CLIENT_SUBCHANNEL)) {
    grpc_channel_stack_builder_destroy(builder);
    return false;
  }
  grpc_channel_stack* stk;
  grpc_error* error = grpc_channel_stack_builder_finish(
      builder, 0, 1, ConnectionDestroy, nullptr,
      reinterpret_cast<void**>(&stk));
  if (error != GRPC_ERROR_NONE) {
    grpc_transport_destroy(connecting_result_.transport);
    gpr_log(GPR_ERROR, "error initializing subchannel stack: %s",
            grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return false;
  }
  intptr_t socket_uuid = connecting_result_.socket_uuid;
  memset(&connecting_result_, 0, sizeof(connecting_result_));
  if (disconnected_) {
    grpc_channel_stack_destroy(stk);
    gpr_free(stk);
    return false;
  }
  // Publish.
  connected_subchannel_.reset(
      New<ConnectedSubchannel>(stk, args_, channelz_node_, socket_uuid));
  gpr_log(GPR_INFO, "New connected subchannel at %p for subchannel %p",
          connected_subchannel_.get(), this);
  // Instantiate state watcher.  Will clean itself up.
  connected_subchannel_watcher_ =
      MakeOrphanable<ConnectedSubchannelStateWatcher>(this);
  return true;
}

void Subchannel::Disconnect() {
  // The subchannel_pool is only used once here in this subchannel, so the
  // access can be outside of the lock.
  if (subchannel_pool_ != nullptr) {
    subchannel_pool_->UnregisterSubchannel(key_);
    subchannel_pool_.reset();
  }
  MutexLock lock(&mu_);
  GPR_ASSERT(!disconnected_);
  disconnected_ = true;
  grpc_connector_shutdown(connector_, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                          "Subchannel disconnected"));
  connected_subchannel_.reset();
  connected_subchannel_watcher_.reset();
}

gpr_atm Subchannel::RefMutate(
    gpr_atm delta, int barrier GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS) {
  gpr_atm old_val = barrier ? gpr_atm_full_fetch_add(&ref_pair_, delta)
                            : gpr_atm_no_barrier_fetch_add(&ref_pair_, delta);
#ifndef NDEBUG
  if (grpc_trace_stream_refcount.enabled()) {
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SUBCHANNEL: %p %12s 0x%" PRIxPTR " -> 0x%" PRIxPTR " [%s]", this,
            purpose, old_val, old_val + delta, reason);
  }
#endif
  return old_val;
}

}  // namespace grpc_core
