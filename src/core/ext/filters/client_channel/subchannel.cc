//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/subchannel.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/health/health_check_client.h"
#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/ext/filters/client_channel/subchannel_stream_client.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/stats.h"
#include "src/core/lib/debug/stats_data.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/handshaker/proxy_mapper_registry.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"

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

using ::grpc_event_engine::experimental::EventEngine;

TraceFlag grpc_trace_subchannel(false, "subchannel");
DebugOnlyTraceFlag grpc_trace_subchannel_refcount(false, "subchannel_refcount");

//
// ConnectedSubchannel
//

ConnectedSubchannel::ConnectedSubchannel(
    grpc_channel_stack* channel_stack, const ChannelArgs& args,
    RefCountedPtr<channelz::SubchannelNode> channelz_subchannel)
    : RefCounted<ConnectedSubchannel>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_subchannel_refcount)
              ? "ConnectedSubchannel"
              : nullptr),
      channel_stack_(channel_stack),
      args_(args),
      channelz_subchannel_(std::move(channelz_subchannel)) {}

ConnectedSubchannel::~ConnectedSubchannel() {
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

size_t ConnectedSubchannel::GetInitialCallSizeEstimate() const {
  return GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)) +
         channel_stack_->call_stack_size;
}

//
// SubchannelCall
//

RefCountedPtr<SubchannelCall> SubchannelCall::Create(Args args,
                                                     grpc_error_handle* error) {
  const size_t allocation_size =
      args.connected_subchannel->GetInitialCallSizeEstimate();
  Arena* arena = args.arena;
  return RefCountedPtr<SubchannelCall>(new (
      arena->Alloc(allocation_size)) SubchannelCall(std::move(args), error));
}

SubchannelCall::SubchannelCall(Args args, grpc_error_handle* error)
    : connected_subchannel_(std::move(args.connected_subchannel)),
      deadline_(args.deadline) {
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  const grpc_call_element_args call_args = {
      callstk,             /* call_stack */
      nullptr,             /* server_transport_data */
      args.context,        /* context */
      args.path.c_slice(), /* path */
      args.start_time,     /* start_time */
      args.deadline,       /* deadline */
      args.arena,          /* arena */
      args.call_combiner   /* call_combiner */
  };
  *error = grpc_call_stack_init(connected_subchannel_->channel_stack(), 1,
                                SubchannelCall::Destroy, this, &call_args);
  if (GPR_UNLIKELY(!error->ok())) {
    gpr_log(GPR_ERROR, "error: %s", StatusToString(*error).c_str());
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
  MaybeInterceptRecvTrailingMetadata(batch);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_CALL_LOG_OP(GPR_INFO, top_elem, batch);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
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

RefCountedPtr<SubchannelCall> SubchannelCall::Ref(const DebugLocation& location,
                                                  const char* reason) {
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

void SubchannelCall::Destroy(void* arg, grpc_error_handle /*error*/) {
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
void GetCallStatus(grpc_status_code* status, Timestamp deadline,
                   grpc_metadata_batch* md_batch, grpc_error_handle error) {
  if (!error.ok()) {
    grpc_error_get_status(error, deadline, status, nullptr, nullptr, nullptr);
  } else {
    *status = md_batch->get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN);
  }
}

}  // namespace

void SubchannelCall::RecvTrailingMetadataReady(void* arg,
                                               grpc_error_handle error) {
  SubchannelCall* call = static_cast<SubchannelCall*>(arg);
  GPR_ASSERT(call->recv_trailing_metadata_ != nullptr);
  grpc_status_code status = GRPC_STATUS_OK;
  GetCallStatus(&status, call->deadline_, call->recv_trailing_metadata_, error);
  channelz::SubchannelNode* channelz_subchannel =
      call->connected_subchannel_->channelz_subchannel();
  GPR_ASSERT(channelz_subchannel != nullptr);
  if (status == GRPC_STATUS_OK) {
    channelz_subchannel->RecordCallSucceeded();
  } else {
    channelz_subchannel->RecordCallFailed();
  }
  Closure::Run(DEBUG_LOCATION, call->original_recv_trailing_metadata_, error);
}

void SubchannelCall::IncrementRefCount() {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void SubchannelCall::IncrementRefCount(const DebugLocation& /*location*/,
                                       const char* reason) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

//
// Subchannel::ConnectedSubchannelStateWatcher
//

class Subchannel::ConnectedSubchannelStateWatcher
    : public AsyncConnectivityStateWatcherInterface {
 public:
  // Must be instantiated while holding c->mu.
  explicit ConnectedSubchannelStateWatcher(WeakRefCountedPtr<Subchannel> c)
      : subchannel_(std::move(c)) {}

  ~ConnectedSubchannelStateWatcher() override {
    subchannel_.reset(DEBUG_LOCATION, "state_watcher");
  }

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    Subchannel* c = subchannel_.get();
    MutexLock lock(&c->mu_);
    // If we're either shutting down or have already seen this connection
    // failure (i.e., c->connected_subchannel_ is null), do nothing.
    //
    // The transport reports TRANSIENT_FAILURE upon GOAWAY but SHUTDOWN
    // upon connection close.  So if the server gracefully shuts down,
    // we will see TRANSIENT_FAILURE followed by SHUTDOWN, but if not, we
    // will see only SHUTDOWN.  Either way, we react to the first one we
    // see, ignoring anything that happens after that.
    if (c->connected_subchannel_ == nullptr) return;
    if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
        new_state == GRPC_CHANNEL_SHUTDOWN) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_subchannel)) {
        gpr_log(GPR_INFO,
                "subchannel %p %s: Connected subchannel %p reports %s: %s", c,
                c->key_.ToString().c_str(), c->connected_subchannel_.get(),
                ConnectivityStateName(new_state), status.ToString().c_str());
      }
      c->connected_subchannel_.reset();
      if (c->channelz_node() != nullptr) {
        c->channelz_node()->SetChildSocket(nullptr);
      }
      // Even though we're reporting IDLE instead of TRANSIENT_FAILURE here,
      // pass along the status from the transport, since it may have
      // keepalive info attached to it that the channel needs.
      // TODO(roth): Consider whether there's a cleaner way to do this.
      c->SetConnectivityStateLocked(GRPC_CHANNEL_IDLE, status);
      c->backoff_.Reset();
    }
  }

  WeakRefCountedPtr<Subchannel> subchannel_;
};

// Asynchronously notifies the \a watcher of a change in the connectvity state
// of \a subchannel to the current \a state. Deletes itself when done.
class Subchannel::AsyncWatcherNotifierLocked {
 public:
  AsyncWatcherNotifierLocked(
      RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface> watcher,
      grpc_connectivity_state state, const absl::Status& status)
      : watcher_(std::move(watcher)) {
    watcher_->PushConnectivityStateChange({state, status});
    ExecCtx::Run(DEBUG_LOCATION,
                 GRPC_CLOSURE_INIT(
                     &closure_,
                     [](void* arg, grpc_error_handle /*error*/) {
                       auto* self =
                           static_cast<AsyncWatcherNotifierLocked*>(arg);
                       self->watcher_->OnConnectivityStateChange();
                       delete self;
                     },
                     this, nullptr),
                 absl::OkStatus());
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
    grpc_connectivity_state state, const absl::Status& status) {
  for (const auto& p : watchers_) {
    new AsyncWatcherNotifierLocked(p.second, state, status);
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
  HealthWatcher(WeakRefCountedPtr<Subchannel> c,
                std::string health_check_service_name)
      : subchannel_(std::move(c)),
        health_check_service_name_(std::move(health_check_service_name)),
        state_(subchannel_->state_ == GRPC_CHANNEL_READY
                   ? GRPC_CHANNEL_CONNECTING
                   : subchannel_->state_) {
    // If the subchannel is already connected, start health checking.
    if (subchannel_->state_ == GRPC_CHANNEL_READY) StartHealthCheckingLocked();
  }

  ~HealthWatcher() override {
    subchannel_.reset(DEBUG_LOCATION, "health_watcher");
  }

  const std::string& health_check_service_name() const {
    return health_check_service_name_;
  }

  grpc_connectivity_state state() const { return state_; }

  void AddWatcherLocked(
      RefCountedPtr<Subchannel::ConnectivityStateWatcherInterface> watcher) {
    new AsyncWatcherNotifierLocked(watcher, state_, status_);
    watcher_list_.AddWatcherLocked(std::move(watcher));
  }

  void RemoveWatcherLocked(
      Subchannel::ConnectivityStateWatcherInterface* watcher) {
    watcher_list_.RemoveWatcherLocked(watcher);
  }

  bool HasWatchers() const { return !watcher_list_.empty(); }

  void NotifyLocked(grpc_connectivity_state state, const absl::Status& status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(subchannel_->mu_) {
    if (state == GRPC_CHANNEL_READY) {
      // If we had not already notified for CONNECTING state, do so now.
      // (We may have missed this earlier, because if the transition
      // from IDLE to CONNECTING to READY was too quick, the connected
      // subchannel may not have sent us a notification for CONNECTING.)
      if (state_ != GRPC_CHANNEL_CONNECTING) {
        state_ = GRPC_CHANNEL_CONNECTING;
        status_ = status;
        watcher_list_.NotifyLocked(state_, status);
      }
      // If we've become connected, start health checking.
      StartHealthCheckingLocked();
    } else {
      state_ = state;
      status_ = status;
      watcher_list_.NotifyLocked(state_, status);
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
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    MutexLock lock(&subchannel_->mu_);
    if (new_state != GRPC_CHANNEL_SHUTDOWN && health_check_client_ != nullptr) {
      state_ = new_state;
      status_ = status;
      watcher_list_.NotifyLocked(new_state, status);
    }
  }

  void StartHealthCheckingLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(subchannel_->mu_) {
    GPR_ASSERT(health_check_client_ == nullptr);
    health_check_client_ = MakeHealthCheckClient(
        health_check_service_name_, subchannel_->connected_subchannel_,
        subchannel_->pollset_set_, subchannel_->channelz_node_, Ref());
  }

  WeakRefCountedPtr<Subchannel> subchannel_;
  std::string health_check_service_name_;
  OrphanablePtr<SubchannelStreamClient> health_check_client_;
  grpc_connectivity_state state_;
  absl::Status status_;
  ConnectivityStateWatcherList watcher_list_;
};

//
// Subchannel::HealthWatcherMap
//

void Subchannel::HealthWatcherMap::AddWatcherLocked(
    WeakRefCountedPtr<Subchannel> subchannel,
    const std::string& health_check_service_name,
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  // If the health check service name is not already present in the map,
  // add it.
  auto it = map_.find(health_check_service_name);
  HealthWatcher* health_watcher;
  if (it == map_.end()) {
    auto w = MakeOrphanable<HealthWatcher>(std::move(subchannel),
                                           health_check_service_name);
    health_watcher = w.get();
    map_.emplace(health_check_service_name, std::move(w));
  } else {
    health_watcher = it->second.get();
  }
  // Add the watcher to the entry.
  health_watcher->AddWatcherLocked(std::move(watcher));
}

void Subchannel::HealthWatcherMap::RemoveWatcherLocked(
    const std::string& health_check_service_name,
    ConnectivityStateWatcherInterface* watcher) {
  auto it = map_.find(health_check_service_name);
  GPR_ASSERT(it != map_.end());
  it->second->RemoveWatcherLocked(watcher);
  // If we just removed the last watcher for this service name, remove
  // the map entry.
  if (!it->second->HasWatchers()) map_.erase(it);
}

void Subchannel::HealthWatcherMap::NotifyLocked(grpc_connectivity_state state,
                                                const absl::Status& status) {
  for (const auto& p : map_) {
    p.second->NotifyLocked(state, status);
  }
}

grpc_connectivity_state
Subchannel::HealthWatcherMap::CheckConnectivityStateLocked(
    Subchannel* subchannel, const std::string& health_check_service_name) {
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
// Subchannel::ConnectivityStateWatcherInterface
//

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

//
// Subchannel
//

namespace {

BackOff::Options ParseArgsForBackoffValues(const ChannelArgs& args,
                                           Duration* min_connect_timeout) {
  const absl::optional<Duration> fixed_reconnect_backoff =
      args.GetDurationFromIntMillis("grpc.testing.fixed_reconnect_backoff_ms");
  if (fixed_reconnect_backoff.has_value()) {
    const Duration backoff =
        std::max(Duration::Milliseconds(100), *fixed_reconnect_backoff);
    *min_connect_timeout = backoff;
    return BackOff::Options()
        .set_initial_backoff(backoff)
        .set_multiplier(1.0)
        .set_jitter(0.0)
        .set_max_backoff(backoff);
  }
  const Duration initial_backoff = std::max(
      Duration::Milliseconds(100),
      args.GetDurationFromIntMillis(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS)
          .value_or(Duration::Seconds(
              GRPC_SUBCHANNEL_INITIAL_CONNECT_BACKOFF_SECONDS)));
  *min_connect_timeout =
      std::max(Duration::Milliseconds(100),
               args.GetDurationFromIntMillis(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS)
                   .value_or(Duration::Seconds(
                       GRPC_SUBCHANNEL_RECONNECT_MIN_TIMEOUT_SECONDS)));
  const Duration max_backoff =
      std::max(Duration::Milliseconds(100),
               args.GetDurationFromIntMillis(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS)
                   .value_or(Duration::Seconds(
                       GRPC_SUBCHANNEL_RECONNECT_MAX_BACKOFF_SECONDS)));
  return BackOff::Options()
      .set_initial_backoff(initial_backoff)
      .set_multiplier(GRPC_SUBCHANNEL_RECONNECT_BACKOFF_MULTIPLIER)
      .set_jitter(GRPC_SUBCHANNEL_RECONNECT_JITTER)
      .set_max_backoff(max_backoff);
}

}  // namespace

Subchannel::Subchannel(SubchannelKey key,
                       OrphanablePtr<SubchannelConnector> connector,
                       const ChannelArgs& args)
    : DualRefCounted<Subchannel>(
          GRPC_TRACE_FLAG_ENABLED(grpc_trace_subchannel_refcount) ? "Subchannel"
                                                                  : nullptr),
      key_(std::move(key)),
      args_(args),
      pollset_set_(grpc_pollset_set_create()),
      connector_(std::move(connector)),
      backoff_(ParseArgsForBackoffValues(args_, &min_connect_timeout_)),
      event_engine_(args_.GetObjectRef<EventEngine>()) {
  // A grpc_init is added here to ensure that grpc_shutdown does not happen
  // until the subchannel is destroyed. Subchannels can persist longer than
  // channels because they maybe reused/shared among multiple channels. As a
  // result the subchannel destruction happens asynchronously to channel
  // destruction. If the last channel destruction triggers a grpc_shutdown
  // before the last subchannel destruction, then there maybe race conditions
  // triggering segmentation faults. To prevent this issue, we call a grpc_init
  // here and a grpc_shutdown in the subchannel destructor.
  InitInternally();
  global_stats().IncrementClientSubchannelsCreated();
  GRPC_CLOSURE_INIT(&on_connecting_finished_, OnConnectingFinished, this,
                    grpc_schedule_on_exec_ctx);
  // Check proxy mapper to determine address to connect to and channel
  // args to use.
  address_for_connect_ = CoreConfiguration::Get()
                             .proxy_mapper_registry()
                             .MapAddress(key_.address(), &args_)
                             .value_or(key_.address());
  // Initialize channelz.
  const bool channelz_enabled = args_.GetBool(GRPC_ARG_ENABLE_CHANNELZ)
                                    .value_or(GRPC_ENABLE_CHANNELZ_DEFAULT);
  if (channelz_enabled) {
    const size_t channel_tracer_max_memory = Clamp(
        args_.GetInt(GRPC_ARG_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE)
            .value_or(GRPC_MAX_CHANNEL_TRACE_EVENT_MEMORY_PER_NODE_DEFAULT),
        0, INT_MAX);
    channelz_node_ = MakeRefCounted<channelz::SubchannelNode>(
        grpc_sockaddr_to_uri(&key_.address())
            .value_or("<unknown address type>"),
        channel_tracer_max_memory);
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
  connector_.reset();
  grpc_pollset_set_destroy(pollset_set_);
  // grpc_shutdown is called here because grpc_init is called in the ctor.
  ShutdownInternally();
}

RefCountedPtr<Subchannel> Subchannel::Create(
    OrphanablePtr<SubchannelConnector> connector,
    const grpc_resolved_address& address, const ChannelArgs& args) {
  SubchannelKey key(address, args);
  auto* subchannel_pool = args.GetObject<SubchannelPoolInterface>();
  GPR_ASSERT(subchannel_pool != nullptr);
  RefCountedPtr<Subchannel> c = subchannel_pool->FindSubchannel(key);
  if (c != nullptr) {
    return c;
  }
  c = MakeRefCounted<Subchannel>(std::move(key), std::move(connector), args);
  // Try to register the subchannel before setting the subchannel pool.
  // Otherwise, in case of a registration race, unreffing c in
  // RegisterSubchannel() will cause c to be tried to be unregistered, while
  // its key maps to a different subchannel.
  RefCountedPtr<Subchannel> registered =
      subchannel_pool->RegisterSubchannel(c->key_, c);
  if (registered == c) c->subchannel_pool_ = subchannel_pool->Ref();
  return registered;
}

void Subchannel::ThrottleKeepaliveTime(int new_keepalive_time) {
  MutexLock lock(&mu_);
  // Only update the value if the new keepalive time is larger.
  if (new_keepalive_time > keepalive_time_) {
    keepalive_time_ = new_keepalive_time;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_subchannel)) {
      gpr_log(GPR_INFO, "subchannel %p %s: throttling keepalive time to %d",
              this, key_.ToString().c_str(), new_keepalive_time);
    }
    args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, new_keepalive_time);
  }
}

channelz::SubchannelNode* Subchannel::channelz_node() {
  return channelz_node_.get();
}

void Subchannel::WatchConnectivityState(
    const absl::optional<std::string>& health_check_service_name,
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
  }
  if (!health_check_service_name.has_value()) {
    new AsyncWatcherNotifierLocked(watcher, state_, status_);
    watcher_list_.AddWatcherLocked(std::move(watcher));
  } else {
    health_watcher_map_.AddWatcherLocked(
        WeakRef(DEBUG_LOCATION, "health_watcher"), *health_check_service_name,
        std::move(watcher));
  }
}

void Subchannel::CancelConnectivityStateWatch(
    const absl::optional<std::string>& health_check_service_name,
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties);
  }
  if (!health_check_service_name.has_value()) {
    watcher_list_.RemoveWatcherLocked(watcher);
  } else {
    health_watcher_map_.RemoveWatcherLocked(*health_check_service_name,
                                            watcher);
  }
}

void Subchannel::RequestConnection() {
  MutexLock lock(&mu_);
  if (state_ == GRPC_CHANNEL_IDLE) {
    StartConnectingLocked();
  }
}

void Subchannel::ResetBackoff() {
  // Hold a ref to ensure cancellation and subsequent deletion of the closure
  // does not eliminate the last ref and destroy the Subchannel before the
  // method returns.
  auto self = WeakRef(DEBUG_LOCATION, "ResetBackoff");
  MutexLock lock(&mu_);
  backoff_.Reset();
  if (state_ == GRPC_CHANNEL_TRANSIENT_FAILURE &&
      event_engine_->Cancel(retry_timer_handle_)) {
    OnRetryTimerLocked();
  } else if (state_ == GRPC_CHANNEL_CONNECTING) {
    next_attempt_time_ = Timestamp::Now();
  }
}

void Subchannel::Orphan() {
  // The subchannel_pool is only used once here in this subchannel, so the
  // access can be outside of the lock.
  if (subchannel_pool_ != nullptr) {
    subchannel_pool_->UnregisterSubchannel(key_, this);
    subchannel_pool_.reset();
  }
  MutexLock lock(&mu_);
  GPR_ASSERT(!shutdown_);
  shutdown_ = true;
  connector_.reset();
  connected_subchannel_.reset();
  health_watcher_map_.ShutdownLocked();
}

void Subchannel::GetOrAddDataProducer(
    UniqueTypeName type,
    std::function<void(DataProducerInterface**)> get_or_add) {
  MutexLock lock(&mu_);
  auto it = data_producer_map_.emplace(type, nullptr).first;
  get_or_add(&it->second);
}

void Subchannel::RemoveDataProducer(DataProducerInterface* data_producer) {
  MutexLock lock(&mu_);
  auto it = data_producer_map_.find(data_producer->type());
  if (it != data_producer_map_.end() && it->second == data_producer) {
    data_producer_map_.erase(it);
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
void Subchannel::SetConnectivityStateLocked(grpc_connectivity_state state,
                                            const absl::Status& status) {
  state_ = state;
  if (status.ok()) {
    status_ = status;
  } else {
    // Augment status message to include IP address.
    status_ = absl::Status(status.code(),
                           absl::StrCat(grpc_sockaddr_to_uri(&key_.address())
                                            .value_or("<unknown address type>"),
                                        ": ", status.message()));
    status.ForEachPayload(
        [this](absl::string_view key, const absl::Cord& value)
        // Want to use ABSL_EXCLUSIVE_LOCKS_REQUIRED(&mu_) here,
        // but that won't work, because we can't pass the lock
        // annotation through absl::Status::ForEachPayload().
        ABSL_NO_THREAD_SAFETY_ANALYSIS { status_.SetPayload(key, value); });
  }
  if (channelz_node_ != nullptr) {
    channelz_node_->UpdateConnectivityState(state);
    channelz_node_->AddTraceEvent(
        channelz::ChannelTrace::Severity::Info,
        grpc_slice_from_static_string(
            SubchannelConnectivityStateChangeString(state)));
  }
  // Notify non-health watchers.
  watcher_list_.NotifyLocked(state, status_);
  // Notify health watchers.
  health_watcher_map_.NotifyLocked(state, status_);
}

void Subchannel::OnRetryTimer() {
  MutexLock lock(&mu_);
  OnRetryTimerLocked();
}

void Subchannel::OnRetryTimerLocked() {
  if (shutdown_) return;
  gpr_log(GPR_INFO, "subchannel %p %s: backoff delay elapsed, reporting IDLE",
          this, key_.ToString().c_str());
  SetConnectivityStateLocked(GRPC_CHANNEL_IDLE, absl::OkStatus());
}

void Subchannel::StartConnectingLocked() {
  // Set next attempt time.
  const Timestamp min_deadline = min_connect_timeout_ + Timestamp::Now();
  next_attempt_time_ = backoff_.NextAttemptTime();
  // Report CONNECTING.
  SetConnectivityStateLocked(GRPC_CHANNEL_CONNECTING, absl::OkStatus());
  // Start connection attempt.
  SubchannelConnector::Args args;
  args.address = &address_for_connect_;
  args.interested_parties = pollset_set_;
  args.deadline = std::max(next_attempt_time_, min_deadline);
  args.channel_args = args_;
  WeakRef(DEBUG_LOCATION, "Connect").release();  // Ref held by callback.
  connector_->Connect(args, &connecting_result_, &on_connecting_finished_);
}

void Subchannel::OnConnectingFinished(void* arg, grpc_error_handle error) {
  WeakRefCountedPtr<Subchannel> c(static_cast<Subchannel*>(arg));
  {
    MutexLock lock(&c->mu_);
    c->OnConnectingFinishedLocked(error);
  }
  c.reset(DEBUG_LOCATION, "Connect");
}

void Subchannel::OnConnectingFinishedLocked(grpc_error_handle error) {
  if (shutdown_) {
    return;
  }
  // If we didn't get a transport or we fail to publish it, report
  // TRANSIENT_FAILURE and start the retry timer.
  // Note that if the connection attempt took longer than the backoff
  // time, then the timer will fire immediately, and we will quickly
  // transition back to IDLE.
  if (connecting_result_.transport == nullptr || !PublishTransportLocked()) {
    const Duration time_until_next_attempt =
        next_attempt_time_ - Timestamp::Now();
    gpr_log(GPR_INFO,
            "subchannel %p %s: connect failed (%s), backing off for %" PRId64
            " ms",
            this, key_.ToString().c_str(), StatusToString(error).c_str(),
            time_until_next_attempt.millis());
    SetConnectivityStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE,
                               grpc_error_to_absl_status(error));
    retry_timer_handle_ = event_engine_->RunAfter(
        time_until_next_attempt,
        [self = WeakRef(DEBUG_LOCATION, "RetryTimer")]() mutable {
          {
            ApplicationCallbackExecCtx callback_exec_ctx;
            ExecCtx exec_ctx;
            self->OnRetryTimer();
            // Subchannel deletion might require an active ExecCtx. So if
            // self.reset() is not called here, the WeakRefCountedPtr destructor
            // may run after the ExecCtx declared in the callback is destroyed.
            // Since subchannel may get destroyed when the WeakRefCountedPtr
            // destructor runs, it may not have an active ExecCtx - thus leading
            // to crashes.
            self.reset();
          }
        });
  }
}

bool Subchannel::PublishTransportLocked() {
  // Construct channel stack.
  ChannelStackBuilderImpl builder("subchannel", GRPC_CLIENT_SUBCHANNEL,
                                  connecting_result_.channel_args);
  builder.SetTransport(connecting_result_.transport);
  if (!CoreConfiguration::Get().channel_init().CreateStack(&builder)) {
    return false;
  }
  absl::StatusOr<RefCountedPtr<grpc_channel_stack>> stk = builder.Build();
  if (!stk.ok()) {
    auto error = absl_status_to_grpc_error(stk.status());
    grpc_transport_destroy(connecting_result_.transport);
    gpr_log(GPR_ERROR,
            "subchannel %p %s: error initializing subchannel stack: %s", this,
            key_.ToString().c_str(), StatusToString(error).c_str());
    return false;
  }
  RefCountedPtr<channelz::SocketNode> socket =
      std::move(connecting_result_.socket_node);
  connecting_result_.Reset();
  if (shutdown_) return false;
  // Publish.
  connected_subchannel_.reset(
      new ConnectedSubchannel(stk->release(), args_, channelz_node_));
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_subchannel)) {
    gpr_log(GPR_INFO, "subchannel %p %s: new connected subchannel at %p", this,
            key_.ToString().c_str(), connected_subchannel_.get());
  }
  if (channelz_node_ != nullptr) {
    channelz_node_->SetChildSocket(std::move(socket));
  }
  // Start watching connected subchannel.
  connected_subchannel_->StartWatch(
      pollset_set_, MakeOrphanable<ConnectedSubchannelStateWatcher>(
                        WeakRef(DEBUG_LOCATION, "state_watcher")));
  // Report initial state.
  SetConnectivityStateLocked(GRPC_CHANNEL_READY, absl::Status());
  return true;
}

}  // namespace grpc_core
