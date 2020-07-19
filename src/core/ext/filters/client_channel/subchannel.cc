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

TraceFlag grpc_trace_subchannel(false, "subchannel");
DebugOnlyTraceFlag grpc_trace_subchannel_refcount(false, "subchannel_refcount");

//
// ConnectedSubchannel
//

ConnectedSubchannel::ConnectedSubchannel(
    grpc_channel_stack* channel_stack, const grpc_channel_args* args,
    RefCountedPtr<channelz::SubchannelNode> channelz_subchannel)
    : RefCounted<ConnectedSubchannel>(&grpc_trace_subchannel_refcount),
      channel_stack_(channel_stack),
      args_(grpc_channel_args_copy(args)),
      channelz_subchannel_(std::move(channelz_subchannel)) {}

ConnectedSubchannel::~ConnectedSubchannel() {
  grpc_channel_args_destroy(args_);
  GRPC_CHANNEL_STACK_UNREF(channel_stack_, "connected_subchannel_dtor");
}

void ConnectedSubchannel::StartWatch(
    grpc_pollset_set* interested_parties,
    OrphanablePtr<ConnectivityStateWatcherInterface> watcher) {
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  op->start_connectivity_watch = std::move(watcher);
  op->start_connectivity_watch_state = GRPC_CHANNEL_READY;
  op->bind_pollset_set = interested_parties;
  grpc_channel_element* elem = grpc_channel_stack_element(channel_stack_, 0);
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

RefCountedPtr<SubchannelCall> SubchannelCall::Create(Args args,
                                                     grpc_error** error) {
  const size_t allocation_size =
      args.connected_subchannel->GetInitialCallSizeEstimate(
          args.parent_data_size);
  Arena* arena = args.arena;
  return RefCountedPtr<SubchannelCall>(new (
      arena->Alloc(allocation_size)) SubchannelCall(std::move(args), error));
}

SubchannelCall::SubchannelCall(Args args, grpc_error** error)
    : connected_subchannel_(std::move(args.connected_subchannel)),
      deadline_(args.deadline) {
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(this);
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
  *error = grpc_call_stack_init(connected_subchannel_->channel_stack(), 1,
                                SubchannelCall::Destroy, this, &call_args);
  if (GPR_UNLIKELY(*error != GRPC_ERROR_NONE)) {
    const char* error_string = grpc_error_string(*error);
    gpr_log(GPR_ERROR, "error: %s", error_string);
    return;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args.pollent);
  auto* channelz_node = connected_subchannel_->channelz_subchannel();
  if (channelz_node != nullptr) {
    channelz_node->RecordCallStarted();
  }
}

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

void SubchannelCall::Unref(const DebugLocation& /*location*/,
                           const char* reason) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

void SubchannelCall::Destroy(void* arg, grpc_error* /*error*/) {
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
  Closure::Run(DEBUG_LOCATION, call->original_recv_trailing_metadata_,
               GRPC_ERROR_REF(error));
}

void SubchannelCall::IncrementRefCount() {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void SubchannelCall::IncrementRefCount(
    const grpc_core::DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

//
// Subchannel::ConnectedSubchannelStateWatcher
//

class Subchannel::ConnectedSubchannelStateWatcher
    : public AsyncConnectivityStateWatcherInterface {
 public:
  // Must be instantiated while holding c->mu.
  explicit ConnectedSubchannelStateWatcher(Subchannel* c) : subchannel_(c) {
    // Steal subchannel ref for connecting.
    GRPC_SUBCHANNEL_WEAK_REF(subchannel_, "state_watcher");
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "connecting");
  }

  ~ConnectedSubchannelStateWatcher() {
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "state_watcher");
  }

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state) override {
    Subchannel* c = subchannel_;
    MutexLock lock(&c->mu_);
    switch (new_state) {
      case GRPC_CHANNEL_TRANSIENT_FAILURE:
      case GRPC_CHANNEL_SHUTDOWN: {
        if (!c->disconnected_ && c->connected_subchannel_ != nullptr) {
          if (grpc_trace_subchannel.enabled()) {
            gpr_log(GPR_INFO,
                    "Connected subchannel %p of subchannel %p has gone into "
                    "%s. Attempting to reconnect.",
                    c->connected_subchannel_.get(), c,
                    ConnectivityStateName(new_state));
          }
          c->connected_subchannel_.reset();
          if (c->channelz_node() != nullptr) {
            c->channelz_node()->SetChildSocket(nullptr);
          }
          c->SetConnectivityStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE);
          c->backoff_begun_ = false;
          c->backoff_.Reset();
        }
        break;
      }
      default: {
        // In principle, this should never happen.  We should not get
        // a callback for READY, because that was the state we started
        // this watch from.  And a connected subchannel should never go
        // from READY to CONNECTING or IDLE.
        c->SetConnectivityStateLocked(new_state);
      }
    }
  }

  Subchannel* subchannel_;
};

// Asynchronously notifies the \a watcher of a change in the connectvity state
// of \a subchannel to the current \a state. Deletes itself when done.
class Subchannel::AsyncWatcherNotifierLocked {
 public:
  AsyncWatcherNotifierLocked(
      RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface> watcher,
      Subchannel* subchannel, grpc_connectivity_state state)
      : watcher_(std::move(watcher)) {
    RefCountedPtr<ConnectedSubchannel> connected_subchannel;
    if (state == GRPC_CHANNEL_READY) {
      connected_subchannel = subchannel->connected_subchannel_;
    }
    watcher_->PushConnectivityStateChange(
        {state, std::move(connected_subchannel)});
    ExecCtx::Run(
        DEBUG_LOCATION,
        GRPC_CLOSURE_INIT(&closure_,
                          [](void* arg, grpc_error* /*error*/) {
                            auto* self =
                                static_cast<AsyncWatcherNotifierLocked*>(arg);
                            self->watcher_->OnConnectivityStateChange();
                            delete self;
                          },
                          this, nullptr),
        GRPC_ERROR_NONE);
  }

 private:
  RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface> watcher_;
  grpc_closure closure_;
};

//
// Subchannel::ConnectivityStateWatcherList
//

void Subchannel::ConnectivityStateWatcherList::AddWatcherLocked(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  watchers_.insert(std::make_pair(watcher.get(), std::move(watcher)));
}

void Subchannel::ConnectivityStateWatcherList::RemoveWatcherLocked(
    ConnectivityStateWatcherInterface* watcher) {
  watchers_.erase(watcher);
}

void Subchannel::ConnectivityStateWatcherList::NotifyLocked(
    Subchannel* subchannel, grpc_connectivity_state state) {
  for (const auto& p : watchers_) {
    new AsyncWatcherNotifierLocked(p.second, subchannel, state);
  }
}

//
// Subchannel::HealthWatcherMap::HealthWatcher
//

// State needed for tracking the connectivity state with a particular
// health check service name.
class Subchannel::HealthWatcherMap::HealthWatcher
    : public AsyncConnectivityStateWatcherInterface {
 public:
  HealthWatcher(Subchannel* c,
                grpc_core::UniquePtr<char> health_check_service_name,
                grpc_connectivity_state subchannel_state)
      : subchannel_(c),
        health_check_service_name_(std::move(health_check_service_name)),
        state_(subchannel_state == GRPC_CHANNEL_READY ? GRPC_CHANNEL_CONNECTING
                                                      : subchannel_state) {
    GRPC_SUBCHANNEL_WEAK_REF(subchannel_, "health_watcher");
    // If the subchannel is already connected, start health checking.
    if (subchannel_state == GRPC_CHANNEL_READY) StartHealthCheckingLocked();
  }

  ~HealthWatcher() {
    GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "health_watcher");
  }

  const char* health_check_service_name() const {
    return health_check_service_name_.get();
  }

  grpc_connectivity_state state() const { return state_; }

  void AddWatcherLocked(
      grpc_connectivity_state initial_state,
      RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface> watcher) {
    if (state_ != initial_state) {
      new AsyncWatcherNotifierLocked(watcher, subchannel_, state_);
    }
    watcher_list_.AddWatcherLocked(std::move(watcher));
  }

  void RemoveWatcherLocked(
      Subchannel::ConnectivityStateWatcherInterface* watcher) {
    watcher_list_.RemoveWatcherLocked(watcher);
  }

  bool HasWatchers() const { return !watcher_list_.empty(); }

  void NotifyLocked(grpc_connectivity_state state) {
    if (state == GRPC_CHANNEL_READY) {
      // If we had not already notified for CONNECTING state, do so now.
      // (We may have missed this earlier, because if the transition
      // from IDLE to CONNECTING to READY was too quick, the connected
      // subchannel may not have sent us a notification for CONNECTING.)
      if (state_ != GRPC_CHANNEL_CONNECTING) {
        state_ = GRPC_CHANNEL_CONNECTING;
        watcher_list_.NotifyLocked(subchannel_, state_);
      }
      // If we've become connected, start health checking.
      StartHealthCheckingLocked();
    } else {
      state_ = state;
      watcher_list_.NotifyLocked(subchannel_, state_);
      // We're not connected, so stop health checking.
      health_check_client_.reset();
    }
  }

  void Orphan() override {
    watcher_list_.Clear();
    health_check_client_.reset();
    Unref();
  }

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state) override {
    MutexLock lock(&subchannel_->mu_);
    if (new_state != GRPC_CHANNEL_SHUTDOWN && health_check_client_ != nullptr) {
      state_ = new_state;
      watcher_list_.NotifyLocked(subchannel_, new_state);
    }
  }

  void StartHealthCheckingLocked() {
    GPR_ASSERT(health_check_client_ == nullptr);
    health_check_client_ = MakeOrphanable<HealthCheckClient>(
        health_check_service_name_.get(), subchannel_->connected_subchannel_,
        subchannel_->pollset_set_, subchannel_->channelz_node_, Ref());
  }

  Subchannel* subchannel_;
  grpc_core::UniquePtr<char> health_check_service_name_;
  OrphanablePtr<HealthCheckClient> health_check_client_;
  grpc_connectivity_state state_;
  ConnectivityStateWatcherList watcher_list_;
};

//
// Subchannel::HealthWatcherMap
//

void Subchannel::HealthWatcherMap::AddWatcherLocked(
    Subchannel* subchannel, grpc_connectivity_state initial_state,
    grpc_core::UniquePtr<char> health_check_service_name,
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  // If the health check service name is not already present in the map,
  // add it.
  auto it = map_.find(health_check_service_name.get());
  HealthWatcher* health_watcher;
  if (it == map_.end()) {
    const char* key = health_check_service_name.get();
    auto w = MakeOrphanable<HealthWatcher>(
        subchannel, std::move(health_check_service_name), subchannel->state_);
    health_watcher = w.get();
    map_[key] = std::move(w);
  } else {
    health_watcher = it->second.get();
  }
  // Add the watcher to the entry.
  health_watcher->AddWatcherLocked(initial_state, std::move(watcher));
}

void Subchannel::HealthWatcherMap::RemoveWatcherLocked(
    const char* health_check_service_name,
    ConnectivityStateWatcherInterface* watcher) {
  auto it = map_.find(health_check_service_name);
  GPR_ASSERT(it != map_.end());
  it->second->RemoveWatcherLocked(watcher);
  // If we just removed the last watcher for this service name, remove
  // the map entry.
  if (!it->second->HasWatchers()) map_.erase(it);
}

void Subchannel::HealthWatcherMap::NotifyLocked(grpc_connectivity_state state) {
  for (const auto& p : map_) {
    p.second->NotifyLocked(state);
  }
}

grpc_connectivity_state
Subchannel::HealthWatcherMap::CheckConnectivityStateLocked(
    Subchannel* subchannel, const char* health_check_service_name) {
  auto it = map_.find(health_check_service_name);
  if (it == map_.end()) {
    // If the health check service name is not found in the map, we're
    // not currently doing a health check for that service name.  If the
    // subchannel's state without health checking is READY, report
    // CONNECTING, since that's what we'd be in as soon as we do start a
    // watch.  Otherwise, report the channel's state without health checking.
    return subchannel->state_ == GRPC_CHANNEL_READY ? GRPC_CHANNEL_CONNECTING
                                                    : subchannel->state_;
  }
  HealthWatcher* health_watcher = it->second.get();
  return health_watcher->state();
}

void Subchannel::HealthWatcherMap::ShutdownLocked() { map_.clear(); }

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

void Subchannel::ConnectivityStateWatcherInterface::PushConnectivityStateChange(
    ConnectivityStateChange state_change) {
  MutexLock lock(&mu_);
  connectivity_state_queue_.push_back(std::move(state_change));
}

Subchannel::ConnectivityStateWatcherInterface::ConnectivityStateChange
Subchannel::ConnectivityStateWatcherInterface::PopConnectivityStateChange() {
  MutexLock lock(&mu_);
  GPR_ASSERT(!connectivity_state_queue_.empty());
  ConnectivityStateChange state_change = connectivity_state_queue_.front();
  connectivity_state_queue_.pop_front();
  return state_change;
}

Subchannel::Subchannel(SubchannelKey* key,
                       OrphanablePtr<SubchannelConnector> connector,
                       const grpc_channel_args* args)
    : key_(key),
      connector_(std::move(connector)),
      backoff_(ParseArgsForBackoffValues(args, &min_connect_timeout_ms_)) {
  GRPC_STATS_INC_CLIENT_SUBCHANNELS_CREATED();
  gpr_atm_no_barrier_store(&ref_pair_, 1 << INTERNAL_REF_BITS);
  pollset_set_ = grpc_pollset_set_create();
  grpc_resolved_address* addr =
      static_cast<grpc_resolved_address*>(gpr_malloc(sizeof(*addr)));
  GetAddressFromSubchannelAddressArg(args, addr);
  grpc_resolved_address* new_address = nullptr;
  grpc_channel_args* new_args = nullptr;
  if (ProxyMapperRegistry::MapAddress(*addr, args, &new_address, &new_args)) {
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
        GetTargetAddress(), channel_tracer_max_memory);
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
    channelz_node_->UpdateConnectivityState(GRPC_CHANNEL_SHUTDOWN);
  }
  grpc_channel_args_destroy(args_);
  connector_.reset();
  grpc_pollset_set_destroy(pollset_set_);
  delete key_;
}

Subchannel* Subchannel::Create(OrphanablePtr<SubchannelConnector> connector,
                               const grpc_channel_args* args) {
  SubchannelKey* key = new SubchannelKey(args);
  SubchannelPoolInterface* subchannel_pool =
      SubchannelPoolInterface::GetSubchannelPoolFromChannelArgs(args);
  GPR_ASSERT(subchannel_pool != nullptr);
  Subchannel* c = subchannel_pool->FindSubchannel(key);
  if (c != nullptr) {
    delete key;
    return c;
  }
  c = new Subchannel(key, std::move(connector), args);
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

void subchannel_destroy(void* arg, grpc_error* /*error*/) {
  Subchannel* self = static_cast<Subchannel*>(arg);
  delete self;
}

}  // namespace

void Subchannel::WeakUnref(GRPC_SUBCHANNEL_REF_EXTRA_ARGS) {
  gpr_atm old_refs;
  old_refs = RefMutate(-static_cast<gpr_atm>(1),
                       1 GRPC_SUBCHANNEL_REF_MUTATE_PURPOSE("WEAK_UNREF"));
  if (old_refs == 1) {
    ExecCtx::Run(DEBUG_LOCATION,
                 GRPC_CLOSURE_CREATE(subchannel_destroy, this,
                                     grpc_schedule_on_exec_ctx),
                 GRPC_ERROR_NONE);
  }
}

Subchannel* Subchannel::RefFromWeakRef() {
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

const char* Subchannel::GetTargetAddress() {
  const grpc_arg* addr_arg =
      grpc_channel_args_find(args_, GRPC_ARG_SUBCHANNEL_ADDRESS);
  const char* addr_str = grpc_channel_arg_get_string(addr_arg);
  GPR_ASSERT(addr_str != nullptr);  // Should have been set by LB policy.
  return addr_str;
}

channelz::SubchannelNode* Subchannel::channelz_node() {
  return channelz_node_.get();
}

grpc_connectivity_state Subchannel::CheckConnectivityState(
    const char* health_check_service_name,
    RefCountedPtr<ConnectedSubchannel>* connected_subchannel) {
  MutexLock lock(&mu_);
  grpc_connectivity_state state;
  if (health_check_service_name == nullptr) {
    state = state_;
  } else {
    state = health_watcher_map_.CheckConnectivityStateLocked(
        this, health_check_service_name);
  }
  if (connected_subchannel != nullptr && state == GRPC_CHANNEL_READY) {
    *connected_subchannel = connected_subchannel_;
  }
  return state;
}

void Subchannel::WatchConnectivityState(
    grpc_connectivity_state initial_state,
    grpc_core::UniquePtr<char> health_check_service_name,
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
  }
  if (health_check_service_name == nullptr) {
    if (state_ != initial_state) {
      new AsyncWatcherNotifierLocked(watcher, this, state_);
    }
    watcher_list_.AddWatcherLocked(std::move(watcher));
  } else {
    health_watcher_map_.AddWatcherLocked(this, initial_state,
                                         std::move(health_check_service_name),
                                         std::move(watcher));
  }
}

void Subchannel::CancelConnectivityStateWatch(
    const char* health_check_service_name,
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties);
  }
  if (health_check_service_name == nullptr) {
    watcher_list_.RemoveWatcherLocked(watcher);
  } else {
    health_watcher_map_.RemoveWatcherLocked(health_check_service_name, watcher);
  }
}

void Subchannel::AttemptToConnect() {
  MutexLock lock(&mu_);
  MaybeStartConnectingLocked();
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
      gpr_strdup(addr->len > 0 ? grpc_sockaddr_to_uri(addr).c_str() : ""));
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

// Note: Must be called with a state that is different from the current state.
void Subchannel::SetConnectivityStateLocked(grpc_connectivity_state state) {
  state_ = state;
  if (channelz_node_ != nullptr) {
    channelz_node_->UpdateConnectivityState(state);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            SubchannelConnectivityStateChangeString(state)));
  }
  // Notify non-health watchers.
  watcher_list_.NotifyLocked(this, state);
  // Notify health watchers.
  health_watcher_map_.NotifyLocked(state);
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
  SubchannelConnector::Args args;
  args.interested_parties = pollset_set_;
  const grpc_millis min_deadline =
      min_connect_timeout_ms_ + ExecCtx::Get()->Now();
  next_attempt_deadline_ = backoff_.NextAttemptTime();
  args.deadline = std::max(next_attempt_deadline_, min_deadline);
  args.channel_args = args_;
  SetConnectivityStateLocked(GRPC_CHANNEL_CONNECTING);
  connector_->Connect(args, &connecting_result_, &on_connecting_finished_);
}

void Subchannel::OnConnectingFinished(void* arg, grpc_error* error) {
  auto* c = static_cast<Subchannel*>(arg);
  const grpc_channel_args* delete_channel_args =
      c->connecting_result_.channel_args;
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
      c->SetConnectivityStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE);
      GRPC_SUBCHANNEL_WEAK_UNREF(c, "connecting");
    }
  }
  GRPC_SUBCHANNEL_WEAK_UNREF(c, "on_connecting_finished");
  grpc_channel_args_destroy(delete_channel_args);
}

namespace {

void ConnectionDestroy(void* arg, grpc_error* /*error*/) {
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
  RefCountedPtr<channelz::SocketNode> socket =
      std::move(connecting_result_.socket_node);
  connecting_result_.Reset();
  if (disconnected_) {
    grpc_channel_stack_destroy(stk);
    gpr_free(stk);
    return false;
  }
  // Publish.
  connected_subchannel_.reset(
      new ConnectedSubchannel(stk, args_, channelz_node_));
  gpr_log(GPR_INFO, "New connected subchannel at %p for subchannel %p",
          connected_subchannel_.get(), this);
  if (channelz_node_ != nullptr) {
    channelz_node_->SetChildSocket(std::move(socket));
  }
  // Start watching connected subchannel.
  connected_subchannel_->StartWatch(
      pollset_set_, MakeOrphanable<ConnectedSubchannelStateWatcher>(this));
  // Report initial state.
  SetConnectivityStateLocked(GRPC_CHANNEL_READY);
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
  connector_.reset();
  connected_subchannel_.reset();
  health_watcher_map_.ShutdownLocked();
}

gpr_atm Subchannel::RefMutate(
    gpr_atm delta, int barrier GRPC_SUBCHANNEL_REF_MUTATE_EXTRA_ARGS) {
  gpr_atm old_val = barrier ? gpr_atm_full_fetch_add(&ref_pair_, delta)
                            : gpr_atm_no_barrier_fetch_add(&ref_pair_, delta);
#ifndef NDEBUG
  if (grpc_trace_subchannel_refcount.enabled()) {
    gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
            "SUBCHANNEL: %p %12s 0x%" PRIxPTR " -> 0x%" PRIxPTR " [%s]", this,
            purpose, old_val, old_val + delta, reason);
  }
#endif
  return old_val;
}

}  // namespace grpc_core
