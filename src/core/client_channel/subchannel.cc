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

#include "src/core/client_channel/subchannel.h"

#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <memory>
#include <new>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>

#include "src/core/channelz/channel_trace.h"
#include "src/core/channelz/channelz.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/subchannel_pool_interface.h"
#include "src/core/handshaker/proxy_mapper_registry.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/backoff/backoff.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/interception_chain.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/alloc.h"
#include "src/core/util/useful.h"

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

//
// ConnectedSubchannel
//

ConnectedSubchannel::ConnectedSubchannel(const ChannelArgs& args)
    : RefCounted<ConnectedSubchannel>(
          GRPC_TRACE_FLAG_ENABLED(subchannel_refcount) ? "ConnectedSubchannel"
                                                       : nullptr),
      args_(args) {}

//
// LegacyConnectedSubchannel
//

class LegacyConnectedSubchannel : public ConnectedSubchannel {
 public:
  LegacyConnectedSubchannel(
      RefCountedPtr<grpc_channel_stack> channel_stack, const ChannelArgs& args,
      RefCountedPtr<channelz::SubchannelNode> channelz_node)
      : ConnectedSubchannel(args),
        channelz_node_(std::move(channelz_node)),
        channel_stack_(std::move(channel_stack)) {}

  ~LegacyConnectedSubchannel() override {
    channel_stack_.reset(DEBUG_LOCATION, "ConnectedSubchannel");
  }

  channelz::SubchannelNode* channelz_node() const {
    return channelz_node_.get();
  }

  void StartWatch(
      grpc_pollset_set* interested_parties,
      OrphanablePtr<ConnectivityStateWatcherInterface> watcher) override {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->start_connectivity_watch = std::move(watcher);
    op->start_connectivity_watch_state = GRPC_CHANNEL_READY;
    op->bind_pollset_set = interested_parties;
    grpc_channel_element* elem =
        grpc_channel_stack_element(channel_stack_.get(), 0);
    elem->filter->start_transport_op(elem, op);
  }

  void Ping(absl::AnyInvocable<void(absl::Status)>) override {
    Crash("call v3 ping method called in legacy impl");
  }

  RefCountedPtr<UnstartedCallDestination> unstarted_call_destination()
      const override {
    Crash("call v3 unstarted_call_destination method called in legacy impl");
  }

  grpc_channel_stack* channel_stack() const override {
    return channel_stack_.get();
  }

  size_t GetInitialCallSizeEstimate() const override {
    return GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)) +
           channel_stack_->call_stack_size;
  }

  void Ping(grpc_closure* on_initiate, grpc_closure* on_ack) override {
    grpc_transport_op* op = grpc_make_transport_op(nullptr);
    op->send_ping.on_initiate = on_initiate;
    op->send_ping.on_ack = on_ack;
    grpc_channel_element* elem =
        grpc_channel_stack_element(channel_stack_.get(), 0);
    elem->filter->start_transport_op(elem, op);
  }

 private:
  RefCountedPtr<channelz::SubchannelNode> channelz_node_;
  RefCountedPtr<grpc_channel_stack> channel_stack_;
};

//
// NewConnectedSubchannel
//

class NewConnectedSubchannel : public ConnectedSubchannel {
 public:
  class TransportCallDestination final : public CallDestination {
   public:
    explicit TransportCallDestination(OrphanablePtr<ClientTransport> transport)
        : transport_(std::move(transport)) {}

    ClientTransport* transport() { return transport_.get(); }

    void HandleCall(CallHandler handler) override {
      transport_->StartCall(std::move(handler));
    }

    void Orphaned() override { transport_.reset(); }

   private:
    OrphanablePtr<ClientTransport> transport_;
  };

  NewConnectedSubchannel(
      RefCountedPtr<UnstartedCallDestination> call_destination,
      RefCountedPtr<TransportCallDestination> transport,
      const ChannelArgs& args)
      : ConnectedSubchannel(args),
        call_destination_(std::move(call_destination)),
        transport_(std::move(transport)) {}

  void StartWatch(
      grpc_pollset_set*,
      OrphanablePtr<ConnectivityStateWatcherInterface> watcher) override {
    transport_->transport()->StartConnectivityWatch(std::move(watcher));
  }

  void Ping(absl::AnyInvocable<void(absl::Status)>) override {
    // TODO(ctiller): add new transport API for this in v3 stack
    Crash("not implemented");
  }

  RefCountedPtr<UnstartedCallDestination> unstarted_call_destination()
      const override {
    return call_destination_;
  }

  grpc_channel_stack* channel_stack() const override { return nullptr; }

  size_t GetInitialCallSizeEstimate() const override { return 0; }

  void Ping(grpc_closure*, grpc_closure*) override {
    Crash("legacy ping method called in call v3 impl");
  }

 private:
  RefCountedPtr<UnstartedCallDestination> call_destination_;
  RefCountedPtr<TransportCallDestination> transport_;
};

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
    : connected_subchannel_(args.connected_subchannel
                                .TakeAsSubclass<LegacyConnectedSubchannel>()),
      deadline_(args.deadline) {
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  const grpc_call_element_args call_args = {
      callstk,              // call_stack
      nullptr,              // server_transport_data
      args.path.c_slice(),  // path
      args.start_time,      // start_time
      args.deadline,        // deadline
      args.arena,           // arena
      args.call_combiner    // call_combiner
  };
  *error = grpc_call_stack_init(connected_subchannel_->channel_stack(), 1,
                                SubchannelCall::Destroy, this, &call_args);
  if (GPR_UNLIKELY(!error->ok())) {
    LOG(ERROR) << "error: " << StatusToString(*error);
    return;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args.pollent);
  auto* channelz_node = connected_subchannel_->channelz_node();
  if (channelz_node != nullptr) {
    channelz_node->RecordCallStarted();
  }
}

void SubchannelCall::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  MaybeInterceptRecvTrailingMetadata(batch);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_TRACE_LOG(channel, INFO)
      << "OP[" << top_elem->filter->name << ":" << top_elem
      << "]: " << grpc_transport_stream_op_batch_string(batch, false);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

grpc_call_stack* SubchannelCall::GetCallStack() {
  return SUBCHANNEL_CALL_TO_CALL_STACK(this);
}

void SubchannelCall::SetAfterCallStackDestroy(grpc_closure* closure) {
  CHECK_EQ(after_call_stack_destroy_, nullptr);
  CHECK_NE(closure, nullptr);
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
  // call, because call->after_call_stack_destroy(), if not null, will free
  // the call arena.
  grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(self), nullptr,
                          after_call_stack_destroy);
  // Automatically reset connected_subchannel. This should be after destroying
  // the call stack, because destroying call stack needs access to the channel
  // stack.
}

void SubchannelCall::MaybeInterceptRecvTrailingMetadata(
    grpc_transport_stream_op_batch* batch) {
  // only intercept payloads with recv trailing.
  if (!batch->recv_trailing_metadata) return;
  // only add interceptor is channelz is enabled.
  if (connected_subchannel_->channelz_node() == nullptr) return;
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    this, grpc_schedule_on_exec_ctx);
  // save some state needed for the interception callback.
  CHECK_EQ(recv_trailing_metadata_, nullptr);
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
  CHECK_NE(call->recv_trailing_metadata_, nullptr);
  grpc_status_code status = GRPC_STATUS_OK;
  GetCallStatus(&status, call->deadline_, call->recv_trailing_metadata_, error);
  channelz::SubchannelNode* channelz_node =
      call->connected_subchannel_->channelz_node();
  CHECK_NE(channelz_node, nullptr);
  if (status == GRPC_STATUS_OK) {
    channelz_node->RecordCallSucceeded();
  } else {
    channelz_node->RecordCallFailed();
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

class Subchannel::ConnectedSubchannelStateWatcher final
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
    {
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
        if (GRPC_TRACE_FLAG_ENABLED(subchannel)) {
          LOG(INFO) << "subchannel " << c << " " << c->key_.ToString()
                    << ": Connected subchannel "
                    << c->connected_subchannel_.get() << " reports "
                    << ConnectivityStateName(new_state) << ": " << status;
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
    // Drain any connectivity state notifications after releasing the mutex.
    c->work_serializer_.DrainQueue();
  }

  WeakRefCountedPtr<Subchannel> subchannel_;
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
    subchannel_->work_serializer_.Schedule(
        [watcher = p.second->Ref(), state, status]() mutable {
          auto* watcher_ptr = watcher.get();
          watcher_ptr->OnConnectivityStateChange(std::move(watcher), state,
                                                 status);
        },
        DEBUG_LOCATION);
  }
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
    : DualRefCounted<Subchannel>(GRPC_TRACE_FLAG_ENABLED(subchannel_refcount)
                                     ? "Subchannel"
                                     : nullptr),
      key_(std::move(key)),
      args_(args),
      pollset_set_(grpc_pollset_set_create()),
      connector_(std::move(connector)),
      watcher_list_(this),
      work_serializer_(args_.GetObjectRef<EventEngine>()),
      backoff_(ParseArgsForBackoffValues(args_, &min_connect_timeout_)),
      event_engine_(args_.GetObjectRef<EventEngine>()) {
  // A grpc_init is added here to ensure that grpc_shutdown does not happen
  // until the subchannel is destroyed. Subchannels can persist longer than
  // channels because they maybe reused/shared among multiple channels. As a
  // result the subchannel destruction happens asynchronously to channel
  // destruction. If the last channel destruction triggers a grpc_shutdown
  // before the last subchannel destruction, then there maybe race conditions
  // triggering segmentation faults. To prevent this issue, we call a
  // grpc_init here and a grpc_shutdown in the subchannel destructor.
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
  CHECK_NE(subchannel_pool, nullptr);
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
    if (GRPC_TRACE_FLAG_ENABLED(subchannel)) {
      LOG(INFO) << "subchannel " << this << " " << key_.ToString()
                << ": throttling keepalive time to " << new_keepalive_time;
    }
    args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, new_keepalive_time);
  }
}

channelz::SubchannelNode* Subchannel::channelz_node() {
  return channelz_node_.get();
}

void Subchannel::WatchConnectivityState(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  {
    MutexLock lock(&mu_);
    grpc_pollset_set* interested_parties = watcher->interested_parties();
    if (interested_parties != nullptr) {
      grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
    }
    work_serializer_.Schedule(
        [watcher = watcher->Ref(), state = state_, status = status_]() mutable {
          auto* watcher_ptr = watcher.get();
          watcher_ptr->OnConnectivityStateChange(std::move(watcher), state,
                                                 status);
        },
        DEBUG_LOCATION);
    watcher_list_.AddWatcherLocked(std::move(watcher));
  }
  // Drain any connectivity state notifications after releasing the mutex.
  work_serializer_.DrainQueue();
}

void Subchannel::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  {
    MutexLock lock(&mu_);
    grpc_pollset_set* interested_parties = watcher->interested_parties();
    if (interested_parties != nullptr) {
      grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties);
    }
    watcher_list_.RemoveWatcherLocked(watcher);
  }
  // Drain any connectivity state notifications after releasing the mutex.
  // (Shouldn't actually be necessary in this case, but better safe than
  // sorry.)
  work_serializer_.DrainQueue();
}

void Subchannel::RequestConnection() {
  {
    MutexLock lock(&mu_);
    if (state_ == GRPC_CHANNEL_IDLE) {
      StartConnectingLocked();
    }
  }
  // Drain any connectivity state notifications after releasing the mutex.
  work_serializer_.DrainQueue();
}

void Subchannel::ResetBackoff() {
  // Hold a ref to ensure cancellation and subsequent deletion of the closure
  // does not eliminate the last ref and destroy the Subchannel before the
  // method returns.
  auto self = WeakRef(DEBUG_LOCATION, "ResetBackoff");
  {
    MutexLock lock(&mu_);
    backoff_.Reset();
    if (state_ == GRPC_CHANNEL_TRANSIENT_FAILURE &&
        event_engine_->Cancel(retry_timer_handle_)) {
      OnRetryTimerLocked();
    } else if (state_ == GRPC_CHANNEL_CONNECTING) {
      next_attempt_time_ = Timestamp::Now();
    }
  }
  // Drain any connectivity state notifications after releasing the mutex.
  work_serializer_.DrainQueue();
}

void Subchannel::Orphaned() {
  // The subchannel_pool is only used once here in this subchannel, so the
  // access can be outside of the lock.
  if (subchannel_pool_ != nullptr) {
    subchannel_pool_->UnregisterSubchannel(key_, this);
    subchannel_pool_.reset();
  }
  {
    MutexLock lock(&mu_);
    CHECK(!shutdown_);
    shutdown_ = true;
    connector_.reset();
    connected_subchannel_.reset();
  }
  // Drain any connectivity state notifications after releasing the mutex.
  work_serializer_.DrainQueue();
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
        grpc_slice_from_cpp_string(absl::StrCat(
            "Subchannel connectivity state changed to ",
            ConnectivityStateName(state),
            status.ok() ? "" : absl::StrCat(": ", status_.ToString()))));
  }
  // Notify watchers.
  watcher_list_.NotifyLocked(state, status_);
}

void Subchannel::OnRetryTimer() {
  {
    MutexLock lock(&mu_);
    OnRetryTimerLocked();
  }
  // Drain any connectivity state notifications after releasing the mutex.
  work_serializer_.DrainQueue();
}

void Subchannel::OnRetryTimerLocked() {
  if (shutdown_) return;
  if (GRPC_TRACE_FLAG_ENABLED(subchannel)) {
    LOG(INFO) << "subchannel " << this << " " << key_.ToString()
              << ": backoff delay elapsed, reporting IDLE";
  }
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
  // Drain any connectivity state notifications after releasing the mutex.
  c->work_serializer_.DrainQueue();
  c.reset(DEBUG_LOCATION, "Connect");
}

void Subchannel::OnConnectingFinishedLocked(grpc_error_handle error) {
  if (shutdown_) {
    connecting_result_.Reset();
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
    if (GRPC_TRACE_FLAG_ENABLED(subchannel)) {
      LOG(INFO) << "subchannel " << this << " " << key_.ToString()
                << ": connect failed (" << StatusToString(error)
                << "), backing off for " << time_until_next_attempt.millis()
                << " ms";
    }
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
            // self.reset() is not called here, the WeakRefCountedPtr
            // destructor may run after the ExecCtx declared in the callback
            // is destroyed. Since subchannel may get destroyed when the
            // WeakRefCountedPtr destructor runs, it may not have an active
            // ExecCtx - thus leading to crashes.
            self.reset();
          }
        });
  }
}

bool Subchannel::PublishTransportLocked() {
  auto socket_node = std::move(connecting_result_.socket_node);
  if (connecting_result_.transport->filter_stack_transport() != nullptr) {
    // Construct channel stack.
    // Builder takes ownership of transport.
    ChannelStackBuilderImpl builder(
        "subchannel", GRPC_CLIENT_SUBCHANNEL,
        connecting_result_.channel_args.SetObject(
            std::exchange(connecting_result_.transport, nullptr)));
    if (!CoreConfiguration::Get().channel_init().CreateStack(&builder)) {
      return false;
    }
    absl::StatusOr<RefCountedPtr<grpc_channel_stack>> stack = builder.Build();
    if (!stack.ok()) {
      connecting_result_.Reset();
      LOG(ERROR) << "subchannel " << this << " " << key_.ToString()
                 << ": error initializing subchannel stack: " << stack.status();
      return false;
    }
    connected_subchannel_ = MakeRefCounted<LegacyConnectedSubchannel>(
        std::move(*stack), args_, channelz_node_);
  } else {
    OrphanablePtr<ClientTransport> transport(
        std::exchange(connecting_result_.transport, nullptr)
            ->client_transport());
    InterceptionChainBuilder builder(
        connecting_result_.channel_args.SetObject(transport.get()));
    if (channelz_node_ != nullptr) {
      // TODO(ctiller): If/when we have a good way to access the subchannel
      // from a filter (maybe GetContext<Subchannel>?), consider replacing
      // these two hooks with a filter so that we can avoid storing two
      // separate refs to the channelz node in each connection.
      builder.AddOnClientInitialMetadata(
          [channelz_node = channelz_node_](ClientMetadata&) {
            channelz_node->RecordCallStarted();
          });
      builder.AddOnServerTrailingMetadata(
          [channelz_node = channelz_node_](ServerMetadata& metadata) {
            if (IsStatusOk(metadata)) {
              channelz_node->RecordCallSucceeded();
            } else {
              channelz_node->RecordCallFailed();
            }
          });
    }
    CoreConfiguration::Get().channel_init().AddToInterceptionChainBuilder(
        GRPC_CLIENT_SUBCHANNEL, builder);
    auto transport_destination =
        MakeRefCounted<NewConnectedSubchannel::TransportCallDestination>(
            std::move(transport));
    auto call_destination = builder.Build(transport_destination);
    if (!call_destination.ok()) {
      connecting_result_.Reset();
      LOG(ERROR) << "subchannel " << this << " " << key_.ToString()
                 << ": error initializing subchannel stack: "
                 << call_destination.status();
      return false;
    }
    connected_subchannel_ = MakeRefCounted<NewConnectedSubchannel>(
        std::move(*call_destination), std::move(transport_destination), args_);
  }
  connecting_result_.Reset();
  // Publish.
  if (GRPC_TRACE_FLAG_ENABLED(subchannel)) {
    LOG(INFO) << "subchannel " << this << " " << key_.ToString()
              << ": new connected subchannel at "
              << connected_subchannel_.get();
  }
  if (channelz_node_ != nullptr) {
    channelz_node_->SetChildSocket(std::move(socket_node));
  }
  // Start watching connected subchannel.
  connected_subchannel_->StartWatch(
      pollset_set_, MakeOrphanable<ConnectedSubchannelStateWatcher>(
                        WeakRef(DEBUG_LOCATION, "state_watcher")));
  // Report initial state.
  SetConnectivityStateLocked(GRPC_CHANNEL_READY, absl::Status());
  return true;
}

ChannelArgs Subchannel::MakeSubchannelArgs(
    const ChannelArgs& channel_args, const ChannelArgs& address_args,
    const RefCountedPtr<SubchannelPoolInterface>& subchannel_pool,
    const std::string& channel_default_authority) {
  // Note that we start with the channel-level args and then apply the
  // per-address args, so that if a value is present in both, the one
  // in the channel-level args is used.  This is particularly important
  // for the GRPC_ARG_DEFAULT_AUTHORITY arg, which we want to allow
  // resolvers to set on a per-address basis only if the application
  // did not explicitly set it at the channel level.
  return channel_args.UnionWith(address_args)
      .SetObject(subchannel_pool)
      // If we haven't already set the default authority arg (i.e., it
      // was not explicitly set by the application nor overridden by
      // the resolver), add it from the channel's default.
      .SetIfUnset(GRPC_ARG_DEFAULT_AUTHORITY, channel_default_authority)
      // Remove channel args that should not affect subchannel
      // uniqueness.
      .Remove(GRPC_ARG_HEALTH_CHECK_SERVICE_NAME)
      .Remove(GRPC_ARG_INHIBIT_HEALTH_CHECKING)
      .Remove(GRPC_ARG_CHANNELZ_CHANNEL_NODE)
      // Remove all keys with the no-subchannel prefix.
      .RemoveAllKeysWithPrefix(GRPC_ARG_NO_SUBCHANNEL_PREFIX);
}

}  // namespace grpc_core
