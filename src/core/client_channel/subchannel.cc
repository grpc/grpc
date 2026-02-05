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

#include "src/core/client_channel/subchannel.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <inttypes.h>
#include <limits.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include "src/core/call/interception_chain.h"
#include "src/core/channelz/channel_trace.h"
#include "src/core/channelz/channelz.h"
#include "src/core/client_channel/buffered_call.h"
#include "src/core/client_channel/client_channel_internal.h"
#include "src/core/client_channel/subchannel_pool_interface.h"
#include "src/core/client_channel/subchannel_stream_limiter.h"
#include "src/core/config/core_configuration.h"
#include "src/core/handshaker/proxy_mapper_registry.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/promise/cancel_callback.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/surface/init_internally.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/telemetry/stats.h"
#include "src/core/telemetry/stats_data.h"
#include "src/core/util/alloc.h"
#include "src/core/util/backoff.h"
#include "src/core/util/debug_location.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "src/core/util/useful.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

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

// To avoid a naming conflict between
// ConnectivityStateWatcherInterface and
// Subchannel::ConnectivityStateWatcherInterface.
using TransportConnectivityStateWatcher = ConnectivityStateWatcherInterface;

//
// Subchannel::Call
//

RefCountedPtr<Subchannel::Call> Subchannel::Call::Ref() {
  IncrementRefCount();
  return RefCountedPtr<Subchannel::Call>(this);
}

RefCountedPtr<Subchannel::Call> Subchannel::Call::Ref(
    const DebugLocation& location, const char* reason) {
  IncrementRefCount(location, reason);
  return RefCountedPtr<Subchannel::Call>(this);
}

//
// Subchannel
//

RefCountedPtr<Subchannel> Subchannel::Create(
    OrphanablePtr<SubchannelConnector> connector,
    const grpc_resolved_address& address, const ChannelArgs& args) {
  if (!IsSubchannelConnectionScalingEnabled()) {
    return OldSubchannel::Create(std::move(connector), address, args);
  }
  return NewSubchannel::Create(std::move(connector), address, args);
}

Subchannel::Subchannel()
    : DualRefCounted<Subchannel>(GRPC_TRACE_FLAG_ENABLED(subchannel_refcount)
                                     ? "Subchannel"
                                     : nullptr) {}

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
      .Remove(GRPC_ARG_MAX_CONNECTIONS_PER_SUBCHANNEL)
      .Remove(GRPC_ARG_MAX_CONNECTIONS_PER_SUBCHANNEL_CAP)
      .Remove(GRPC_ARG_CHANNELZ_CHANNEL_NODE)
      // Remove all keys with the no-subchannel prefix.
      .RemoveAllKeysWithPrefix(GRPC_ARG_NO_SUBCHANNEL_PREFIX);
}

//
// OldSubchannel::ConnectedSubchannel
//

class OldSubchannel::ConnectedSubchannel
    : public RefCounted<ConnectedSubchannel> {
 public:
  const ChannelArgs& args() const { return args_; }

  virtual void StartWatch(
      grpc_pollset_set* interested_parties,
      OrphanablePtr<TransportConnectivityStateWatcher> watcher) = 0;

  // Methods for v3 stack.
  virtual void Ping(absl::AnyInvocable<void(absl::Status)> on_ack) = 0;
  virtual RefCountedPtr<UnstartedCallDestination> unstarted_call_destination()
      const = 0;

  // Methods for legacy stack.
  virtual RefCountedPtr<Call> CreateCall(CreateCallArgs args,
                                         grpc_error_handle* error) = 0;
  virtual void Ping(grpc_closure* on_initiate, grpc_closure* on_ack) = 0;

 protected:
  explicit ConnectedSubchannel(const ChannelArgs& args)
      : RefCounted<ConnectedSubchannel>(
            GRPC_TRACE_FLAG_ENABLED(subchannel_refcount) ? "ConnectedSubchannel"
                                                         : nullptr),
        args_(args) {}

 private:
  ChannelArgs args_;
};

//
// OldSubchannel::LegacyConnectedSubchannel
//

class OldSubchannel::LegacyConnectedSubchannel final
    : public ConnectedSubchannel {
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

  void StartWatch(
      grpc_pollset_set* interested_parties,
      OrphanablePtr<TransportConnectivityStateWatcher> watcher) override {
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

  RefCountedPtr<Call> CreateCall(CreateCallArgs args,
                                 grpc_error_handle* error) override {
    const size_t allocation_size =
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)) +
        channel_stack_->call_stack_size;
    Arena* arena = args.arena;
    return RefCountedPtr<SubchannelCall>(
        new (arena->Alloc(allocation_size)) SubchannelCall(
            RefAsSubclass<LegacyConnectedSubchannel>(), args, error));
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
  class SubchannelCall final : public Call {
   public:
    SubchannelCall(
        RefCountedPtr<LegacyConnectedSubchannel> connected_subchannel,
        CreateCallArgs args, grpc_error_handle* error);

    void StartTransportStreamOpBatch(
        grpc_transport_stream_op_batch* batch) override;

    void SetAfterCallStackDestroy(grpc_closure* closure) override;

    // When refcount drops to 0, destroys itself and the associated call stack,
    // but does NOT free the memory because it's in the call arena.
    void Unref() override;
    void Unref(const DebugLocation& location, const char* reason) override;

   private:
    // If channelz is enabled, intercepts recv_trailing so that we may check the
    // status and associate it to a subchannel.
    void MaybeInterceptRecvTrailingMetadata(
        grpc_transport_stream_op_batch* batch);

    static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error);

    // Interface of RefCounted<>.
    void IncrementRefCount() override;
    void IncrementRefCount(const DebugLocation& location,
                           const char* reason) override;

    static void Destroy(void* arg, grpc_error_handle error);

    RefCountedPtr<LegacyConnectedSubchannel> connected_subchannel_;
    grpc_closure* after_call_stack_destroy_ = nullptr;
    // State needed to support channelz interception of recv trailing metadata.
    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ = nullptr;
    grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
    Timestamp deadline_;
  };

  RefCountedPtr<channelz::SubchannelNode> channelz_node_;
  RefCountedPtr<grpc_channel_stack> channel_stack_;
};

//
// OldSubchannel::LegacyConnectedSubchannel::SubchannelCall
//

OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::SubchannelCall(
    RefCountedPtr<LegacyConnectedSubchannel> connected_subchannel,
    CreateCallArgs args, grpc_error_handle* error)
    : connected_subchannel_(std::move(connected_subchannel)),
      deadline_(args.deadline) {
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  const grpc_call_element_args call_args = {
      callstk,            // call_stack
      nullptr,            // server_transport_data
      args.start_time,    // start_time
      args.deadline,      // deadline
      args.arena,         // arena
      args.call_combiner  // call_combiner
  };
  *error = grpc_call_stack_init(connected_subchannel_->channel_stack_.get(), 1,
                                SubchannelCall::Destroy, this, &call_args);
  if (GPR_UNLIKELY(!error->ok())) {
    LOG(ERROR) << "error: " << StatusToString(*error);
    return;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args.pollent);
  if (connected_subchannel_->channelz_node_ != nullptr) {
    connected_subchannel_->channelz_node_->RecordCallStarted();
  }
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch) {
  MaybeInterceptRecvTrailingMetadata(batch);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_TRACE_LOG(channel, INFO)
      << "OP[" << top_elem->filter->name << ":" << top_elem
      << "]: " << grpc_transport_stream_op_batch_string(batch, false);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    SetAfterCallStackDestroy(grpc_closure* closure) {
  GRPC_CHECK_EQ(after_call_stack_destroy_, nullptr);
  GRPC_CHECK_NE(closure, nullptr);
  after_call_stack_destroy_ = closure;
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::Unref() {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::Unref(
    const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::Destroy(
    void* arg, grpc_error_handle /*error*/) {
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

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    MaybeInterceptRecvTrailingMetadata(grpc_transport_stream_op_batch* batch) {
  // only intercept payloads with recv trailing.
  if (!batch->recv_trailing_metadata) return;
  // only add interceptor is channelz is enabled.
  if (connected_subchannel_->channelz_node_ == nullptr) return;
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    this, grpc_schedule_on_exec_ctx);
  // save some state needed for the interception callback.
  GRPC_CHECK_EQ(recv_trailing_metadata_, nullptr);
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

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    RecvTrailingMetadataReady(void* arg, grpc_error_handle error) {
  SubchannelCall* call = static_cast<SubchannelCall*>(arg);
  GRPC_CHECK_NE(call->recv_trailing_metadata_, nullptr);
  grpc_status_code status = GRPC_STATUS_OK;
  GetCallStatus(&status, call->deadline_, call->recv_trailing_metadata_, error);
  channelz::SubchannelNode* channelz_node =
      call->connected_subchannel_->channelz_node_.get();
  GRPC_CHECK_NE(channelz_node, nullptr);
  if (status == GRPC_STATUS_OK) {
    channelz_node->RecordCallSucceeded();
  } else {
    channelz_node->RecordCallFailed();
  }
  Closure::Run(DEBUG_LOCATION, call->original_recv_trailing_metadata_, error);
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    IncrementRefCount() {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void OldSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    IncrementRefCount(const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

//
// OldSubchannel::NewConnectedSubchannel
//

class OldSubchannel::NewConnectedSubchannel final : public ConnectedSubchannel {
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
      OrphanablePtr<TransportConnectivityStateWatcher> watcher) override {
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

  RefCountedPtr<Call> CreateCall(CreateCallArgs, grpc_error_handle*) override {
    Crash("legacy CreateCall() called on v3 impl");
  }

  void Ping(grpc_closure*, grpc_closure*) override {
    Crash("legacy ping method called in call v3 impl");
  }

 private:
  RefCountedPtr<UnstartedCallDestination> call_destination_;
  RefCountedPtr<TransportCallDestination> transport_;
};

//
// OldSubchannel::ConnectedSubchannelStateWatcher
//

class OldSubchannel::ConnectedSubchannelStateWatcher final
    : public AsyncConnectivityStateWatcherInterface {
 public:
  // Must be instantiated while holding c->mu.
  explicit ConnectedSubchannelStateWatcher(WeakRefCountedPtr<OldSubchannel> c)
      : subchannel_(std::move(c)) {}

  ~ConnectedSubchannelStateWatcher() override {
    subchannel_.reset(DEBUG_LOCATION, "state_watcher");
  }

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    OldSubchannel* c = subchannel_.get();
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
      if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
          new_state == GRPC_CHANNEL_SHUTDOWN) {
        RefCountedPtr<ConnectedSubchannel> connected_subchannel =
            std::move(c->connected_subchannel_);
        if (connected_subchannel == nullptr) return;
        GRPC_TRACE_LOG(subchannel, INFO)
            << "subchannel " << c << " " << c->key_.ToString()
            << ": Connected subchannel " << connected_subchannel.get()
            << " reports " << ConnectivityStateName(new_state) << ": "
            << status;
        // If the subchannel was created from an endpoint, then we report
        // TRANSIENT_FAILURE here instead of IDLE. The subchannel will never
        // leave TRANSIENT_FAILURE state, because there is no way for us to
        // establish a new connection.
        //
        // Otherwise, we report IDLE here. Note that even though we're not
        // reporting TRANSIENT_FAILURE, we pass along the status from the
        // transport, since it may have keepalive info attached to it that the
        // channel needs.
        // TODO(roth): Consider whether there's a cleaner way to propagate the
        // keepalive info.
        c->SetConnectivityStateLocked(c->created_from_endpoint_
                                          ? GRPC_CHANNEL_TRANSIENT_FAILURE
                                          : GRPC_CHANNEL_IDLE,
                                      status);
        c->backoff_.Reset();
      }
    }
  }

  WeakRefCountedPtr<OldSubchannel> subchannel_;
};

//
// OldSubchannel::ConnectivityStateWatcherList
//

void OldSubchannel::ConnectivityStateWatcherList::AddWatcherLocked(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  watchers_.insert(std::move(watcher));
}

void OldSubchannel::ConnectivityStateWatcherList::RemoveWatcherLocked(
    ConnectivityStateWatcherInterface* watcher) {
  watchers_.erase(watcher);
}

void OldSubchannel::ConnectivityStateWatcherList::NotifyLocked(
    grpc_connectivity_state state, const absl::Status& status) {
  for (const auto& watcher : watchers_) {
    subchannel_->work_serializer_.Run([watcher, state, status]() {
      watcher->OnConnectivityStateChange(state, status);
    });
  }
}

void OldSubchannel::ConnectivityStateWatcherList::NotifyOnKeepaliveUpdateLocked(
    Duration new_keepalive_time) {
  for (const auto& watcher : watchers_) {
    subchannel_->work_serializer_.Run([watcher, new_keepalive_time]() {
      watcher->OnKeepaliveUpdate(new_keepalive_time);
    });
  }
}

uint32_t
OldSubchannel::ConnectivityStateWatcherList::GetMaxConnectionsPerSubchannel()
    const {
  uint32_t max_connections_per_subchannel = 1;
  for (const auto& watcher : watchers_) {
    max_connections_per_subchannel =
        std::max(max_connections_per_subchannel,
                 watcher->max_connections_per_subchannel());
  }
  return max_connections_per_subchannel;
}

//
// OldSubchannel
//

namespace {

BackOff::Options ParseArgsForBackoffValues(const ChannelArgs& args,
                                           Duration* min_connect_timeout) {
  const std::optional<Duration> fixed_reconnect_backoff =
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

OldSubchannel::OldSubchannel(SubchannelKey key,
                             OrphanablePtr<SubchannelConnector> connector,
                             const ChannelArgs& args)
    : key_(std::move(key)),
      created_from_endpoint_(args.Contains(GRPC_ARG_SUBCHANNEL_ENDPOINT)),
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
    GRPC_CHANNELZ_LOG(channelz_node_) << "subchannel created";
    channelz_node_->SetChannelArgs(args_);
    args_ = args_.SetObject<channelz::BaseNode>(channelz_node_);
  }
}

OldSubchannel::~OldSubchannel() {
  if (channelz_node_ != nullptr) {
    GRPC_CHANNELZ_LOG(channelz_node_) << "Subchannel destroyed";
    channelz_node_->UpdateConnectivityState(GRPC_CHANNEL_SHUTDOWN);
  }
  connector_.reset();
  grpc_pollset_set_destroy(pollset_set_);
  // grpc_shutdown is called here because grpc_init is called in the ctor.
  ShutdownInternally();
}

RefCountedPtr<Subchannel> OldSubchannel::Create(
    OrphanablePtr<SubchannelConnector> connector,
    const grpc_resolved_address& address, const ChannelArgs& args) {
  SubchannelKey key(address, args);
  auto* subchannel_pool = args.GetObject<SubchannelPoolInterface>();
  GRPC_CHECK_NE(subchannel_pool, nullptr);
  RefCountedPtr<OldSubchannel> c =
      subchannel_pool->FindSubchannel(key).TakeAsSubclass<OldSubchannel>();
  if (c != nullptr) {
    return c;
  }
  c = MakeRefCounted<OldSubchannel>(std::move(key), std::move(connector), args);
  if (c->created_from_endpoint_) {
    // We don't interact with the subchannel pool in this case.
    // Instead, we unconditionally return the newly created subchannel.
    // Before returning, we explicitly trigger a connection attempt
    // by calling RequestConnection(), which sets the subchannel’s
    // connectivity state to CONNECTING.
    c->RequestConnection();
    return c;
  }
  // Try to register the subchannel before setting the subchannel pool.
  // Otherwise, in case of a registration race, unreffing c in
  // RegisterSubchannel() will cause c to be tried to be unregistered, while
  // its key maps to a different subchannel.
  RefCountedPtr<OldSubchannel> registered =
      subchannel_pool->RegisterSubchannel(c->key_, c)
          .TakeAsSubclass<OldSubchannel>();
  if (registered == c) c->subchannel_pool_ = subchannel_pool->Ref();
  return registered;
}

void OldSubchannel::ThrottleKeepaliveTime(Duration new_keepalive_time) {
  MutexLock lock(&mu_);
  ThrottleKeepaliveTimeLocked(new_keepalive_time);
}

void OldSubchannel::ThrottleKeepaliveTimeLocked(Duration new_keepalive_time) {
  // Only update the value if the new keepalive time is larger.
  if (new_keepalive_time > keepalive_time_) {
    keepalive_time_ = new_keepalive_time;
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << this << " " << key_.ToString()
        << ": throttling keepalive time to " << new_keepalive_time;
    args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, new_keepalive_time.millis());
  }
}

channelz::SubchannelNode* OldSubchannel::channelz_node() {
  return channelz_node_.get();
}

void OldSubchannel::WatchConnectivityState(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
  }
  work_serializer_.Run(
      [watcher, state = state_, status = status_]() {
        watcher->OnConnectivityStateChange(state, status);
      },
      DEBUG_LOCATION);
  watcher_list_.AddWatcherLocked(std::move(watcher));
}

void OldSubchannel::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties);
  }
  watcher_list_.RemoveWatcherLocked(watcher);
}

void OldSubchannel::RequestConnection() {
  MutexLock lock(&mu_);
  if (state_ == GRPC_CHANNEL_IDLE) {
    StartConnectingLocked();
  }
}

void OldSubchannel::ResetBackoff() {
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

void OldSubchannel::Orphaned() {
  // The subchannel_pool is only used once here in this subchannel, so the
  // access can be outside of the lock.
  if (subchannel_pool_ != nullptr) {
    subchannel_pool_->UnregisterSubchannel(key_, this);
    subchannel_pool_.reset();
  }
  MutexLock lock(&mu_);
  GRPC_CHECK(!shutdown_);
  shutdown_ = true;
  connector_.reset();
  connected_subchannel_.reset();
}

void OldSubchannel::GetOrAddDataProducer(
    UniqueTypeName type,
    std::function<void(DataProducerInterface**)> get_or_add) {
  MutexLock lock(&mu_);
  auto it = data_producer_map_.emplace(type, nullptr).first;
  get_or_add(&it->second);
}

void OldSubchannel::RemoveDataProducer(DataProducerInterface* data_producer) {
  MutexLock lock(&mu_);
  auto it = data_producer_map_.find(data_producer->type());
  if (it != data_producer_map_.end() && it->second == data_producer) {
    data_producer_map_.erase(it);
  }
}

// Note: Must be called with a state that is different from the current state.
void OldSubchannel::SetConnectivityStateLocked(grpc_connectivity_state state,
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
    if (status.ok()) {
      GRPC_CHANNELZ_LOG(channelz_node_)
          << "Subchannel connectivity state changed to "
          << ConnectivityStateName(state);
    } else {
      GRPC_CHANNELZ_LOG(channelz_node_)
          << "Subchannel connectivity state changed to "
          << ConnectivityStateName(state) << ": " << status;
    }
  }
  // Notify watchers.
  watcher_list_.NotifyLocked(state, status_);
}

void OldSubchannel::OnRetryTimer() {
  MutexLock lock(&mu_);
  OnRetryTimerLocked();
}

void OldSubchannel::OnRetryTimerLocked() {
  if (shutdown_) return;
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": backoff delay elapsed, reporting IDLE";
  SetConnectivityStateLocked(GRPC_CHANNEL_IDLE, absl::OkStatus());
}

void OldSubchannel::StartConnectingLocked() {
  // Set next attempt time.
  const Timestamp now = Timestamp::Now();
  const Timestamp min_deadline = now + min_connect_timeout_;
  next_attempt_time_ = now + backoff_.NextAttemptDelay();
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

void OldSubchannel::OnConnectingFinished(void* arg, grpc_error_handle error) {
  WeakRefCountedPtr<OldSubchannel> c(static_cast<OldSubchannel*>(arg));
  {
    MutexLock lock(&c->mu_);
    c->OnConnectingFinishedLocked(error);
  }
  c.reset(DEBUG_LOCATION, "Connect");
}

void OldSubchannel::OnConnectingFinishedLocked(grpc_error_handle error) {
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
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << this << " " << key_.ToString()
        << ": connect failed (" << StatusToString(error) << ")"
        << (created_from_endpoint_
                ? ", no retry will be attempted (created from endpoint); "
                  "remaining in TRANSIENT_FAILURE"
                : ", backing off for " +
                      std::to_string(time_until_next_attempt.millis()) + " ms");
    SetConnectivityStateLocked(GRPC_CHANNEL_TRANSIENT_FAILURE,
                               grpc_error_to_absl_status(error));
    if (created_from_endpoint_) return;
    retry_timer_handle_ = event_engine_->RunAfter(
        time_until_next_attempt,
        [self = WeakRef(DEBUG_LOCATION, "RetryTimer")
                    .TakeAsSubclass<OldSubchannel>()]() mutable {
          {
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

bool OldSubchannel::PublishTransportLocked() {
  auto socket_node = connecting_result_.transport->GetSocketNode();
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
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": new connected subchannel at " << connected_subchannel_.get();
  if (channelz_node_ != nullptr) {
    if (socket_node != nullptr) {
      socket_node->AddParent(channelz_node_.get());
    }
  }
  connected_subchannel_->StartWatch(
      pollset_set_, MakeOrphanable<ConnectedSubchannelStateWatcher>(
                        WeakRef(DEBUG_LOCATION, "state_watcher")
                            .TakeAsSubclass<OldSubchannel>()));
  // Report initial state.
  SetConnectivityStateLocked(GRPC_CHANNEL_READY, absl::Status());
  return true;
}

RefCountedPtr<Subchannel::Call> OldSubchannel::CreateCall(
    CreateCallArgs args, grpc_error_handle* error) {
  auto connected_subchannel = GetConnectedSubchannel();
  if (connected_subchannel == nullptr) return nullptr;
  return connected_subchannel->CreateCall(args, error);
}

RefCountedPtr<UnstartedCallDestination> OldSubchannel::call_destination() {
  auto connected_subchannel = GetConnectedSubchannel();
  if (connected_subchannel == nullptr) return nullptr;
  return connected_subchannel->unstarted_call_destination();
}

void OldSubchannel::Ping(absl::AnyInvocable<void(absl::Status)>) {
  // TODO(ctiller): Implement
}

absl::Status OldSubchannel::Ping(grpc_closure* on_initiate,
                                 grpc_closure* on_ack) {
  auto connected_subchannel = GetConnectedSubchannel();
  if (connected_subchannel == nullptr) {
    return absl::UnavailableError("no connection");
  }
  connected_subchannel->Ping(on_initiate, on_ack);
  return absl::OkStatus();
}

RefCountedPtr<OldSubchannel::ConnectedSubchannel>
OldSubchannel::GetConnectedSubchannel() {
  MutexLock lock(&mu_);
  return connected_subchannel_;
}

//
// NewSubchannel::ConnectedSubchannel
//

class NewSubchannel::ConnectedSubchannel
    : public DualRefCounted<ConnectedSubchannel> {
 public:
  ~ConnectedSubchannel() override {
    subchannel_.reset(DEBUG_LOCATION, "ConnectedSubchannel");
  }

  const ChannelArgs& args() const { return args_; }
  NewSubchannel* subchannel() const { return subchannel_.get(); }

  virtual void StartWatch(
      grpc_pollset_set* interested_parties,
      OrphanablePtr<TransportConnectivityStateWatcher> watcher) = 0;

  // Methods for v3 stack.
  virtual void Ping(absl::AnyInvocable<void(absl::Status)> on_ack) = 0;
  virtual RefCountedPtr<UnstartedCallDestination> unstarted_call_destination()
      const = 0;

  // Methods for legacy stack.
  virtual RefCountedPtr<Call> CreateCall(CreateCallArgs args,
                                         grpc_error_handle* error) = 0;
  virtual void Ping(grpc_closure* on_initiate, grpc_closure* on_ack) = 0;

  // Returns true if there is quota for another RPC to start on this
  // connection.
  GRPC_MUST_USE_RESULT bool SetMaxConcurrentStreams(
      uint32_t max_concurrent_streams) {
    return stream_limiter_.SetMaxConcurrentStreams(max_concurrent_streams);
  }

  // Returns true if the RPC can start.
  bool GetQuotaForRpc() {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "subchannel " << subchannel_.get() << " connection " << this
        << ": attempting to get quota for an RPC...";
    bool result = stream_limiter_.GetQuotaForRpc();
    GRPC_TRACE_LOG(subchannel_call, INFO) << "  quota acquired: " << result;
    return result;
  }

  // Returns true if this RPC finishing brought the connection below quota.
  bool ReturnQuotaForRpc() { return stream_limiter_.ReturnQuotaForRpc(); }

 protected:
  explicit ConnectedSubchannel(WeakRefCountedPtr<NewSubchannel> subchannel,
                               const ChannelArgs& args,
                               uint32_t max_concurrent_streams)
      : DualRefCounted<ConnectedSubchannel>(
            GRPC_TRACE_FLAG_ENABLED(subchannel_refcount) ? "ConnectedSubchannel"
                                                         : nullptr),
        subchannel_(std::move(subchannel)),
        args_(args),
        stream_limiter_(max_concurrent_streams) {}

 private:
  WeakRefCountedPtr<NewSubchannel> subchannel_;
  ChannelArgs args_;
  SubchannelStreamLimiter stream_limiter_;
};

//
// NewSubchannel::LegacyConnectedSubchannel
//

class NewSubchannel::LegacyConnectedSubchannel final
    : public ConnectedSubchannel {
 public:
  LegacyConnectedSubchannel(
      WeakRefCountedPtr<NewSubchannel> subchannel,
      RefCountedPtr<grpc_channel_stack> channel_stack, const ChannelArgs& args,
      RefCountedPtr<channelz::SubchannelNode> channelz_node,
      uint32_t max_concurrent_streams)
      : ConnectedSubchannel(std::move(subchannel), args,
                            max_concurrent_streams),
        channelz_node_(std::move(channelz_node)),
        channel_stack_(std::move(channel_stack)) {}

  void Orphaned() override {
    channel_stack_.reset(DEBUG_LOCATION, "ConnectedSubchannel");
  }

  void StartWatch(
      grpc_pollset_set* interested_parties,
      OrphanablePtr<TransportConnectivityStateWatcher> watcher) override {
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

  RefCountedPtr<Call> CreateCall(CreateCallArgs args,
                                 grpc_error_handle* error) override {
    const size_t allocation_size =
        GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(SubchannelCall)) +
        channel_stack_->call_stack_size;
    Arena* arena = args.arena;
    return RefCountedPtr<SubchannelCall>(
        new (arena->Alloc(allocation_size)) SubchannelCall(
            RefAsSubclass<LegacyConnectedSubchannel>(), args, error));
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
  class SubchannelCall final : public Call {
   public:
    SubchannelCall(
        RefCountedPtr<LegacyConnectedSubchannel> connected_subchannel,
        CreateCallArgs args, grpc_error_handle* error);

    void StartTransportStreamOpBatch(
        grpc_transport_stream_op_batch* batch) override;

    void SetAfterCallStackDestroy(grpc_closure* closure) override;

    // When refcount drops to 0, destroys itself and the associated call stack,
    // but does NOT free the memory because it's in the call arena.
    void Unref() override;
    void Unref(const DebugLocation& location, const char* reason) override;

   private:
    // If channelz is enabled, intercepts recv_trailing so that we may check the
    // status and associate it to a subchannel.
    void MaybeInterceptRecvTrailingMetadata(
        grpc_transport_stream_op_batch* batch);

    static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error);

    // Interface of RefCounted<>.
    void IncrementRefCount() override;
    void IncrementRefCount(const DebugLocation& location,
                           const char* reason) override;

    static void Destroy(void* arg, grpc_error_handle error);

    // Returns the quota for this RPC.  If that brings the connection
    // below quota, then try to drain the queue.
    void MaybeReturnQuota();

    RefCountedPtr<LegacyConnectedSubchannel> connected_subchannel_;
    grpc_closure* after_call_stack_destroy_ = nullptr;
    // State needed to support channelz interception of recv trailing metadata.
    grpc_closure recv_trailing_metadata_ready_;
    grpc_closure* original_recv_trailing_metadata_ = nullptr;
    grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
    Timestamp deadline_;
    bool returned_quota_ = false;
  };

  RefCountedPtr<channelz::SubchannelNode> channelz_node_;
  RefCountedPtr<grpc_channel_stack> channel_stack_;
};

//
// NewSubchannel::LegacyConnectedSubchannel::SubchannelCall
//

NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::SubchannelCall(
    RefCountedPtr<LegacyConnectedSubchannel> connected_subchannel,
    CreateCallArgs args, grpc_error_handle* error)
    : connected_subchannel_(std::move(connected_subchannel)),
      deadline_(args.deadline) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << connected_subchannel_->subchannel() << " connection "
      << connected_subchannel_.get() << ": created call " << this;
  grpc_call_stack* callstk = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  const grpc_call_element_args call_args = {
      callstk,            // call_stack
      nullptr,            // server_transport_data
      args.start_time,    // start_time
      args.deadline,      // deadline
      args.arena,         // arena
      args.call_combiner  // call_combiner
  };
  *error = grpc_call_stack_init(connected_subchannel_->channel_stack_.get(), 1,
                                SubchannelCall::Destroy, this, &call_args);
  if (GPR_UNLIKELY(!error->ok())) {
    LOG(ERROR) << "error: " << StatusToString(*error);
    return;
  }
  grpc_call_stack_set_pollset_or_pollset_set(callstk, args.pollent);
  if (connected_subchannel_->channelz_node_ != nullptr) {
    connected_subchannel_->channelz_node_->RecordCallStarted();
  }
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    StartTransportStreamOpBatch(grpc_transport_stream_op_batch* batch) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << connected_subchannel_->subchannel() << " connection "
      << connected_subchannel_.get() << " call " << this << ": starting batch: "
      << grpc_transport_stream_op_batch_string(batch, false);
  MaybeInterceptRecvTrailingMetadata(batch);
  grpc_call_stack* call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(this);
  grpc_call_element* top_elem = grpc_call_stack_element(call_stack, 0);
  GRPC_TRACE_LOG(channel, INFO)
      << "OP[" << top_elem->filter->name << ":" << top_elem
      << "]: " << grpc_transport_stream_op_batch_string(batch, false);
  top_elem->filter->start_transport_stream_op_batch(top_elem, batch);
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    SetAfterCallStackDestroy(grpc_closure* closure) {
  GRPC_CHECK_EQ(after_call_stack_destroy_, nullptr);
  GRPC_CHECK_NE(closure, nullptr);
  after_call_stack_destroy_ = closure;
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::Unref() {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::Unref(
    const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::Destroy(
    void* arg, grpc_error_handle /*error*/) {
  SubchannelCall* self = static_cast<SubchannelCall*>(arg);
  // Just in case we didn't already take care of this in the
  // recv_trailing_metadata callback, return the quota now.
  self->MaybeReturnQuota();
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

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    MaybeInterceptRecvTrailingMetadata(grpc_transport_stream_op_batch* batch) {
  // only intercept payloads with recv trailing.
  if (!batch->recv_trailing_metadata) return;
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                    this, grpc_schedule_on_exec_ctx);
  // save some state needed for the interception callback.
  GRPC_CHECK_EQ(recv_trailing_metadata_, nullptr);
  recv_trailing_metadata_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata;
  original_recv_trailing_metadata_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_metadata_ready_;
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    RecvTrailingMetadataReady(void* arg, grpc_error_handle error) {
  SubchannelCall* call = static_cast<SubchannelCall*>(arg);
  GRPC_CHECK_NE(call->recv_trailing_metadata_, nullptr);
  // Return MAX_CONCURRENT_STREAMS quota.
  call->MaybeReturnQuota();
  // If channelz is enabled, record the success or failure of the call.
  if (auto* channelz_node = call->connected_subchannel_->channelz_node_.get();
      channelz_node != nullptr) {
    grpc_status_code status = GRPC_STATUS_OK;
    GetCallStatus(&status, call->deadline_, call->recv_trailing_metadata_,
                  error);
    GRPC_CHECK_NE(channelz_node, nullptr);
    if (status == GRPC_STATUS_OK) {
      channelz_node->RecordCallSucceeded();
    } else {
      channelz_node->RecordCallFailed();
    }
  }
  Closure::Run(DEBUG_LOCATION, call->original_recv_trailing_metadata_, error);
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    MaybeReturnQuota() {
  if (returned_quota_) return;  // Already returned.
  returned_quota_ = true;
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << connected_subchannel_->subchannel() << " connection "
      << connected_subchannel_.get() << ": call " << this
      << " complete, returning quota";
  if (connected_subchannel_->ReturnQuotaForRpc()) {
    connected_subchannel_->subchannel()->RetryQueuedRpcs();
  }
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    IncrementRefCount() {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void NewSubchannel::LegacyConnectedSubchannel::SubchannelCall::
    IncrementRefCount(const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

//
// NewSubchannel::QueuedCallInterface
//

class NewSubchannel::QueuedCallInterface {
 public:
  virtual ~QueuedCallInterface() = default;

  virtual void ResumeOnConnectionLocked(
      ConnectedSubchannel* connected_subchannel)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&NewSubchannel::mu_) = 0;

  virtual void FailLocked(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&NewSubchannel::mu_) = 0;
};

//
// NewSubchannel::QueuedCall
//

class NewSubchannel::QueuedCall final
    : public Subchannel::Call,
      public NewSubchannel::QueuedCallInterface {
 public:
  QueuedCall(WeakRefCountedPtr<NewSubchannel> subchannel, CreateCallArgs args);
  ~QueuedCall() override;

  void StartTransportStreamOpBatch(
      grpc_transport_stream_op_batch* batch) override;

  void SetAfterCallStackDestroy(grpc_closure* closure) override;

  // Interface of RefCounted<>.
  // When refcount drops to 0, the dtor is called, but we do not
  // free memory, because it's allocated on the arena.
  void Unref() override {
    if (ref_count_.Unref()) this->~QueuedCall();
  }
  void Unref(const DebugLocation& location, const char* reason) override {
    if (ref_count_.Unref(location, reason)) this->~QueuedCall();
  }

  void ResumeOnConnectionLocked(ConnectedSubchannel* connected_subchannel)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(&NewSubchannel::mu_);

  void FailLocked(absl::Status status) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&NewSubchannel::mu_);

 private:
  // Allow RefCountedPtr<> to access IncrementRefCount().
  template <typename T>
  friend class RefCountedPtr;

  class Canceller;

  // Interface of RefCounted<>.
  void IncrementRefCount() override { ref_count_.Ref(); }
  void IncrementRefCount(const DebugLocation& location,
                         const char* reason) override {
    ref_count_.Ref(location, reason);
  }

  static void RecvTrailingMetadataReady(void* arg, grpc_error_handle error);

  RefCount ref_count_;
  WeakRefCountedPtr<NewSubchannel> subchannel_;
  CreateCallArgs args_;

  // Note that unlike in the resolver and LB code, the subchannel code
  // adds the call to the queue before adding batches to buffered_call_,
  // so it's possible that the subchannel will get quota for the call
  // and try to resume it before buffered_call_ contains any batches.
  // In that case, we will not be holding the call combiner here, so we
  // need a mutex for synchronization.
  Mutex mu_ ABSL_ACQUIRED_AFTER(NewSubchannel::mu_);
  grpc_closure* after_call_stack_destroy_ ABSL_GUARDED_BY(&mu_) = nullptr;
  grpc_error_handle cancel_error_ ABSL_GUARDED_BY(&mu_);
  BufferedCall buffered_call_ ABSL_GUARDED_BY(&mu_);
  RefCountedPtr<Call> subchannel_call_ ABSL_GUARDED_BY(&mu_);

  // The queue holds a raw pointer to this QueuedCall object, and this
  // is a reference to that pointer.  If the call gets cancelled while
  // in the queue, we set this pointer to null.  The queuing code knows to
  // ignore null pointers when draining the queue, which ensures that we
  // don't try to dequeue this call after it's been cancelled.
  QueuedCallInterface*& queue_entry_;

  Canceller* canceller_ ABSL_GUARDED_BY(&NewSubchannel::mu_);

  std::atomic<bool> is_retriable_{false};
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ = nullptr;
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
};

// Handles call combiner cancellation.  We don't yield the call combiner
// when queuing the call, which means that if the call gets cancelled
// while we're queued, the surface will be unable to immediately start the
// cancel_stream batch to let us know about the cancellation.  Instead,
// this object registers itself with the call combiner to be called if
// the call is cancelled.  In that case, it removes the call from the
// queue and fails any pending batches, thus immediately releasing the
// call combiner and allowing the cancellation to proceed.
class NewSubchannel::QueuedCall::Canceller final {
 public:
  explicit Canceller(RefCountedPtr<QueuedCall> call) : call_(std::move(call)) {
    GRPC_CLOSURE_INIT(&cancel_, CancelLocked, this, nullptr);
    call_->args_.call_combiner->SetNotifyOnCancel(&cancel_);
  }

 private:
  static void CancelLocked(void* arg, grpc_error_handle error) {
    auto* self = static_cast<Canceller*>(arg);
    bool cancelled = false;
    {
      MutexLock lock(&self->call_->subchannel_->mu_);
      if (self->call_->canceller_ == self && !error.ok()) {
        GRPC_TRACE_LOG(subchannel_call, INFO)
            << "subchannel " << self->call_->subchannel_.get()
            << " queued call " << self->call_.get()
            << ": call combiner canceller called";
        // Remove from queue.
        self->call_->queue_entry_ = nullptr;
        cancelled = true;
      }
    }
    if (cancelled) {
      MutexLock lock(&self->call_->mu_);
      // Fail pending batches on the call.
      self->call_->buffered_call_.Fail(
          error, BufferedCall::YieldCallCombinerIfPendingBatchesFound);
    }
    delete self;
  }

  RefCountedPtr<QueuedCall> call_;
  grpc_closure cancel_;
};

NewSubchannel::QueuedCall::QueuedCall(
    WeakRefCountedPtr<NewSubchannel> subchannel, CreateCallArgs args)
    : subchannel_(std::move(subchannel)),
      args_(args),
      buffered_call_(args_.call_combiner, &subchannel_call_trace),
      queue_entry_(subchannel_->queued_calls_.emplace_back(this)) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << ": created queued call " << this
      << ", queue size=" << subchannel_->queued_calls_.size();
  canceller_ = new Canceller(Ref().TakeAsSubclass<QueuedCall>());
}

NewSubchannel::QueuedCall::~QueuedCall() {
  GRPC_TRACE_LOG(subchannel_call, INFO) << "subchannel " << subchannel_.get()
                                        << ": destroying queued call " << this;
  if (after_call_stack_destroy_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, after_call_stack_destroy_, absl::OkStatus());
  }
}

void NewSubchannel::QueuedCall::SetAfterCallStackDestroy(
    grpc_closure* closure) {
  GRPC_CHECK_NE(closure, nullptr);
  MutexLock lock(&mu_);
  if (subchannel_call_ != nullptr) {
    subchannel_call_->SetAfterCallStackDestroy(closure);
  } else {
    GRPC_CHECK_EQ(after_call_stack_destroy_, nullptr);
    after_call_stack_destroy_ = closure;
  }
}

void NewSubchannel::QueuedCall::StartTransportStreamOpBatch(
    grpc_transport_stream_op_batch* batch) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << " queued call " << this
      << ": starting batch: "
      << grpc_transport_stream_op_batch_string(batch, false);
  MutexLock lock(&mu_);
  // If we already have a real subchannel call, pass the batch down to it.
  if (subchannel_call_ != nullptr) {
    subchannel_call_->StartTransportStreamOpBatch(batch);
    return;
  }
  // Intercept recv_trailing_metadata, so that we can mark the call as
  // eligible for transparent retries if we fail it due to all
  // connections failing.
  if (batch->recv_trailing_metadata) {
    GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_, RecvTrailingMetadataReady,
                      this, grpc_schedule_on_exec_ctx);
    GRPC_CHECK_EQ(recv_trailing_metadata_, nullptr);
    recv_trailing_metadata_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata;
    original_recv_trailing_metadata_ =
        batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
    batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
        &recv_trailing_metadata_ready_;
  }
  // If we've previously been cancelled, immediately fail the new batch.
  if (!cancel_error_.ok()) {
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(batch, cancel_error_,
                                                       args_.call_combiner);
    return;
  }
  // Handle cancellation batches.
  if (batch->cancel_stream) {
    cancel_error_ = batch->payload->cancel_stream.cancel_error;
    buffered_call_.Fail(cancel_error_, BufferedCall::NoYieldCallCombiner);
    // Note: This will release the call combiner.
    grpc_transport_stream_op_batch_finish_with_failure(batch, cancel_error_,
                                                       args_.call_combiner);
    return;
  }
  // Enqueue the batch.
  buffered_call_.EnqueueBatch(batch);
  // We hold on to the call combiner for the send_initial_metadata batch,
  // but yield it for other batches.  This ensures that we are holding on
  // to the call combiner exactly once when we are ready to resume.
  if (!batch->send_initial_metadata) {
    GRPC_CALL_COMBINER_STOP(args_.call_combiner,
                            "batch does not include send_initial_metadata");
  }
}

void NewSubchannel::QueuedCall::RecvTrailingMetadataReady(
    void* arg, grpc_error_handle error) {
  QueuedCall* call = static_cast<QueuedCall*>(arg);
  GRPC_CHECK_NE(call->recv_trailing_metadata_, nullptr);
  if (call->is_retriable_.load()) {
    call->recv_trailing_metadata_->Set(GrpcStreamNetworkState(),
                                       GrpcStreamNetworkState::kNotSentOnWire);
  }
  Closure::Run(DEBUG_LOCATION, call->original_recv_trailing_metadata_, error);
}

void NewSubchannel::QueuedCall::ResumeOnConnectionLocked(
    ConnectedSubchannel* connected_subchannel) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << " queued call " << this
      << ": resuming on connected_subchannel " << connected_subchannel;
  canceller_ = nullptr;
  MutexLock lock(&mu_);
  grpc_error_handle error;
  subchannel_call_ = connected_subchannel->CreateCall(args_, &error);
  if (after_call_stack_destroy_ != nullptr) {
    subchannel_call_->SetAfterCallStackDestroy(after_call_stack_destroy_);
    after_call_stack_destroy_ = nullptr;
  }
  // It's possible that the subchannel will get quota for the call
  // and try to resume it before buffered_call_ contains any batches.
  // In that case, we will not be holding the call combiner here, so we
  // must not yeild it.  That's why we use
  // YieldCallCombinerIfPendingBatchesFound here.
  if (!error.ok()) {
    buffered_call_.Fail(error,
                        BufferedCall::YieldCallCombinerIfPendingBatchesFound);
  } else {
    buffered_call_.Resume(
        [subchannel_call =
             subchannel_call_](grpc_transport_stream_op_batch* batch) {
          // This will release the call combiner.
          subchannel_call->StartTransportStreamOpBatch(batch);
        },
        BufferedCall::YieldCallCombinerIfPendingBatchesFound);
  }
}

void NewSubchannel::QueuedCall::FailLocked(absl::Status status) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << " queued call " << this
      << ": failing: " << status;
  canceller_ = nullptr;
  is_retriable_.store(true);
  MutexLock lock(&mu_);
  cancel_error_ = status;
  buffered_call_.Fail(status,
                      BufferedCall::YieldCallCombinerIfPendingBatchesFound);
}

//
// NewSubchannel::NewConnectedSubchannel
//

class NewSubchannel::NewConnectedSubchannel final : public ConnectedSubchannel {
 public:
  NewConnectedSubchannel(WeakRefCountedPtr<NewSubchannel> subchannel_in,
                         OrphanablePtr<ClientTransport> transport,
                         const ChannelArgs& args,
                         uint32_t max_concurrent_streams, absl::Status* status)
      : ConnectedSubchannel(std::move(subchannel_in), args,
                            max_concurrent_streams) {
    InterceptionChainBuilder builder(args.SetObject(transport.get()));
    builder.AddOnServerTrailingMetadata(
        [self = WeakRefAsSubclass<NewConnectedSubchannel>()](ServerMetadata&) {
          if (self->ReturnQuotaForRpc()) {
            self->subchannel()->RetryQueuedRpcs();
          }
        });
    if (subchannel()->channelz_node_ != nullptr) {
      // TODO(ctiller): If/when we have a good way to access the subchannel
      // from a filter (maybe GetContext<Subchannel>?), consider replacing
      // these two hooks with a filter so that we can avoid storing two
      // separate refs to the channelz node in each connection.
      builder.AddOnClientInitialMetadata(
          [channelz_node = subchannel()->channelz_node_](ClientMetadata&) {
            channelz_node->RecordCallStarted();
          });
      builder.AddOnServerTrailingMetadata(
          [channelz_node =
               subchannel()->channelz_node_](ServerMetadata& metadata) {
            if (IsStatusOk(metadata)) {
              channelz_node->RecordCallSucceeded();
            } else {
              channelz_node->RecordCallFailed();
            }
          });
    }
    CoreConfiguration::Get().channel_init().AddToInterceptionChainBuilder(
        GRPC_CLIENT_SUBCHANNEL, builder);
    transport_destination_ =
        MakeRefCounted<TransportCallDestination>(std::move(transport));
    auto call_destination = builder.Build(transport_destination_);
    if (!call_destination.ok()) {
      *status = call_destination.status();
    } else {
      call_destination_ = std::move(*call_destination);
    }
  }

  void Orphaned() override {
    call_destination_.reset();
    transport_destination_.reset();
  }

  void StartWatch(
      grpc_pollset_set*,
      OrphanablePtr<TransportConnectivityStateWatcher> watcher) override {
    transport_destination_->transport()->StartConnectivityWatch(
        std::move(watcher));
  }

  void Ping(absl::AnyInvocable<void(absl::Status)>) override {
    // TODO(ctiller): add new transport API for this in v3 stack
    Crash("not implemented");
  }

  RefCountedPtr<UnstartedCallDestination> unstarted_call_destination()
      const override {
    return call_destination_;
  }

  RefCountedPtr<Call> CreateCall(CreateCallArgs, grpc_error_handle*) override {
    Crash("legacy CreateCall() called on v3 impl");
  }

  void Ping(grpc_closure*, grpc_closure*) override {
    Crash("legacy ping method called in call v3 impl");
  }

 private:
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

  RefCountedPtr<UnstartedCallDestination> call_destination_;
  RefCountedPtr<TransportCallDestination> transport_destination_;
};

//
// NewSubchannel::QueuingUnstartedCallDestination
//

class NewSubchannel::QueuingUnstartedCallDestination final
    : public UnstartedCallDestination,
      public NewSubchannel::QueuedCallInterface {
 public:
  explicit QueuingUnstartedCallDestination(
      WeakRefCountedPtr<NewSubchannel> subchannel)
      : subchannel_(std::move(subchannel)),
        queue_entry_(subchannel_->queued_calls_.emplace_back(this)) {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "subchannel " << subchannel_.get() << ": created queued call "
        << this << ", queue size=" << subchannel_->queued_calls_.size();
  }

  void Orphaned() override {
    MutexLock lock(&subchannel_->mu_);
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "subchannel " << subchannel_.get() << " queued call " << this
        << ": orphaned, queue_entry_valid_=" << queue_entry_valid_;
    if (queue_entry_valid_) queue_entry_ = nullptr;
  }

  void StartCall(UnstartedCallHandler unstarted_call_handler) override {
    unstarted_call_handler.SpawnInfallible(
        "subchannel_queue",
        [unstarted_call_handler,
         self = RefAsSubclass<QueuingUnstartedCallDestination>()]() mutable {
          return Map(
              self->connection_latch_.Wait(),
              [unstarted_call_handler = std::move(unstarted_call_handler),
               self](absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>
                         connection) mutable {
                if (!connection.ok()) {
                  GRPC_TRACE_LOG(subchannel_call, INFO)
                      << "subchannel " << self->subchannel_.get()
                      << " queued call " << self.get()
                      << ": failing: " << connection.status();
                  // Mark the call as eligible for transparent retries.
                  ServerMetadataHandle trailing_metadata =
                      ServerMetadataFromStatus(connection.status());
                  trailing_metadata->Set(
                      GrpcStreamNetworkState(),
                      GrpcStreamNetworkState::kNotSentOnWire);
                  unstarted_call_handler.PushServerTrailingMetadata(
                      std::move(trailing_metadata));
                  return;
                }
                GRPC_TRACE_LOG(subchannel_call, INFO)
                    << "subchannel " << self->subchannel_.get()
                    << " queued call " << self.get()
                    << ": using connection: " << connection->get();
                (*connection)->StartCall(std::move(unstarted_call_handler));
              });
        });
  }

  void ResumeOnConnectionLocked(ConnectedSubchannel* connected_subchannel)
      override ABSL_EXCLUSIVE_LOCKS_REQUIRED(&NewSubchannel::mu_) {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "subchannel " << subchannel_.get() << " queued call " << this
        << ": resuming call on connection " << connected_subchannel;
    queue_entry_valid_ = false;
    connection_latch_.Set(connected_subchannel->unstarted_call_destination());
  }

  void FailLocked(absl::Status status) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&NewSubchannel::mu_) {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "subchannel " << subchannel_.get() << " queued call " << this
        << ": failing call: " << status;
    queue_entry_valid_ = false;
    connection_latch_.Set(std::move(status));
  }

 private:
  WeakRefCountedPtr<NewSubchannel> subchannel_;
  bool queue_entry_valid_ ABSL_GUARDED_BY(&NewSubchannel::mu_) = true;
  QueuedCallInterface*& queue_entry_;
  Latch<absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>>
      connection_latch_;
};

//
// NewSubchannel::ConnectionStateWatcher
//

class NewSubchannel::ConnectionStateWatcher final
    : public Transport::StateWatcher {
 public:
  explicit ConnectionStateWatcher(
      WeakRefCountedPtr<ConnectedSubchannel> connected_subchannel)
      : connected_subchannel_(std::move(connected_subchannel)) {}

  void OnDisconnect(absl::Status status,
                    DisconnectInfo disconnect_info) override {
    NewSubchannel* subchannel = connected_subchannel_->subchannel();
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << subchannel << " " << subchannel->key_.ToString()
        << ": connected subchannel " << connected_subchannel_.get()
        << " reports disconnection: " << status;
    MutexLock lock(&subchannel->mu_);
    // Handle keepalive update.
    if (disconnect_info.keepalive_time.has_value()) {
      subchannel->ThrottleKeepaliveTimeLocked(*disconnect_info.keepalive_time);
      subchannel->watcher_list_.NotifyOnKeepaliveUpdateLocked(
          *disconnect_info.keepalive_time);
    }
    // Remove the connection from the subchannel's list of connections.
    subchannel->RemoveConnectionLocked(connected_subchannel_.get());
    // If this was the last connection, then fail all queued RPCs and
    // update the connectivity state.
    if (subchannel->connections_.empty()) {
      subchannel->FailAllQueuedRpcsLocked(
          absl::UnavailableError("subchannel lost all connections"));
      subchannel->MaybeUpdateConnectivityStateLocked();
    } else {
      // Otherwise, retry queued RPCs, which may trigger a new
      // connection attempt.
      subchannel->RetryQueuedRpcsLocked();
    }
    // Reset backoff.
    subchannel->backoff_.Reset();
  }

  void OnPeerMaxConcurrentStreamsUpdate(
      uint32_t max_concurrent_streams,
      std::unique_ptr<MaxConcurrentStreamsUpdateDoneHandle> /*on_done*/)
      override {
    NewSubchannel* subchannel = connected_subchannel_->subchannel();
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << subchannel << " " << subchannel->key_.ToString()
        << ": connection " << connected_subchannel_.get()
        << ": setting MAX_CONCURRENT_STREAMS=" << max_concurrent_streams;
    if (connected_subchannel_->SetMaxConcurrentStreams(
            max_concurrent_streams)) {
      subchannel->RetryQueuedRpcs();
    }
  }

  grpc_pollset_set* interested_parties() const override {
    return connected_subchannel_->subchannel()->pollset_set_;
  }

 private:
  WeakRefCountedPtr<ConnectedSubchannel> connected_subchannel_;
};

//
// NewSubchannel::ConnectivityStateWatcherList
//

void NewSubchannel::ConnectivityStateWatcherList::AddWatcherLocked(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  watchers_.insert(std::move(watcher));
}

void NewSubchannel::ConnectivityStateWatcherList::RemoveWatcherLocked(
    ConnectivityStateWatcherInterface* watcher) {
  watchers_.erase(watcher);
}

void NewSubchannel::ConnectivityStateWatcherList::NotifyLocked(
    grpc_connectivity_state state, const absl::Status& status) {
  for (const auto& watcher : watchers_) {
    subchannel_->work_serializer_.Run([watcher, state, status]() {
      watcher->OnConnectivityStateChange(state, status);
    });
  }
}

void NewSubchannel::ConnectivityStateWatcherList::NotifyOnKeepaliveUpdateLocked(
    Duration new_keepalive_time) {
  for (const auto& watcher : watchers_) {
    subchannel_->work_serializer_.Run([watcher, new_keepalive_time]() {
      watcher->OnKeepaliveUpdate(new_keepalive_time);
    });
  }
}

uint32_t
NewSubchannel::ConnectivityStateWatcherList::GetMaxConnectionsPerSubchannel()
    const {
  uint32_t max_connections_per_subchannel = 1;
  for (const auto& watcher : watchers_) {
    max_connections_per_subchannel =
        std::max(max_connections_per_subchannel,
                 watcher->max_connections_per_subchannel());
  }
  return max_connections_per_subchannel;
}

//
// NewSubchannel
//

NewSubchannel::NewSubchannel(SubchannelKey key,
                             OrphanablePtr<SubchannelConnector> connector,
                             const ChannelArgs& args)
    : key_(std::move(key)),
      created_from_endpoint_(args.Contains(GRPC_ARG_SUBCHANNEL_ENDPOINT)),
      args_(args),
      pollset_set_(grpc_pollset_set_create()),
      connector_(std::move(connector)),
      watcher_list_(this),
      work_serializer_(args_.GetObjectRef<EventEngine>()),
      backoff_(ParseArgsForBackoffValues(args_, &min_connect_timeout_)),
      event_engine_(args_.GetObjectRef<EventEngine>()) {
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString() << ": created";
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
    GRPC_CHANNELZ_LOG(channelz_node_) << "subchannel created";
    channelz_node_->SetChannelArgs(args_);
    args_ = args_.SetObject<channelz::BaseNode>(channelz_node_);
  }
}

NewSubchannel::~NewSubchannel() {
  if (channelz_node_ != nullptr) {
    GRPC_CHANNELZ_LOG(channelz_node_) << "Subchannel destroyed";
    channelz_node_->UpdateConnectivityState(GRPC_CHANNEL_SHUTDOWN);
  }
  connector_.reset();
  grpc_pollset_set_destroy(pollset_set_);
  // grpc_shutdown is called here because grpc_init is called in the ctor.
  ShutdownInternally();
}

RefCountedPtr<Subchannel> NewSubchannel::Create(
    OrphanablePtr<SubchannelConnector> connector,
    const grpc_resolved_address& address, const ChannelArgs& args) {
  SubchannelKey key(address, args);
  auto* subchannel_pool = args.GetObject<SubchannelPoolInterface>();
  GRPC_CHECK_NE(subchannel_pool, nullptr);
  RefCountedPtr<NewSubchannel> c =
      subchannel_pool->FindSubchannel(key).TakeAsSubclass<NewSubchannel>();
  if (c != nullptr) {
    return c;
  }
  c = MakeRefCounted<NewSubchannel>(std::move(key), std::move(connector), args);
  if (c->created_from_endpoint_) {
    // We don't interact with the subchannel pool in this case.
    // Instead, we unconditionally return the newly created subchannel.
    // Before returning, we explicitly trigger a connection attempt
    // by calling RequestConnection(), which sets the subchannel's
    // connectivity state to CONNECTING.
    c->RequestConnection();
    return c;
  }
  // Try to register the subchannel before setting the subchannel pool.
  // Otherwise, in case of a registration race, unreffing c in
  // RegisterSubchannel() will cause c to be tried to be unregistered, while
  // its key maps to a different subchannel.
  RefCountedPtr<NewSubchannel> registered =
      subchannel_pool->RegisterSubchannel(c->key_, c)
          .TakeAsSubclass<NewSubchannel>();
  if (registered == c) c->subchannel_pool_ = subchannel_pool->Ref();
  return registered;
}

void NewSubchannel::ThrottleKeepaliveTime(Duration new_keepalive_time) {
  MutexLock lock(&mu_);
  ThrottleKeepaliveTimeLocked(new_keepalive_time);
}

void NewSubchannel::ThrottleKeepaliveTimeLocked(Duration new_keepalive_time) {
  // Only update the value if the new keepalive time is larger.
  if (new_keepalive_time > keepalive_time_) {
    keepalive_time_ = new_keepalive_time;
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << this << " " << key_.ToString()
        << ": throttling keepalive time to " << new_keepalive_time;
    args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, new_keepalive_time.millis());
  }
}

channelz::SubchannelNode* NewSubchannel::channelz_node() {
  return channelz_node_.get();
}

void NewSubchannel::WatchConnectivityState(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_add_pollset_set(pollset_set_, interested_parties);
  }
  work_serializer_.Run(
      [watcher, state = state_, status = ConnectivityStatusToReportLocked()]() {
        watcher->OnConnectivityStateChange(state, status);
      },
      DEBUG_LOCATION);
  watcher_list_.AddWatcherLocked(std::move(watcher));
  // The max_connections_per_subchannel setting may have changed, so
  // this may trigger another connection attempt.
  RetryQueuedRpcsLocked();
}

void NewSubchannel::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties);
  }
  watcher_list_.RemoveWatcherLocked(watcher);
}

void NewSubchannel::RequestConnection() {
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": RequestConnection()";
  MutexLock lock(&mu_);
  if (state_ == GRPC_CHANNEL_IDLE) {
    StartConnectingLocked();
  }
}

void NewSubchannel::ResetBackoff() {
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString() << ": ResetBackoff()";
  // Hold a ref to ensure cancellation and subsequent deletion of the closure
  // does not eliminate the last ref and destroy the Subchannel before the
  // method returns.
  auto self = WeakRef(DEBUG_LOCATION, "ResetBackoff");
  MutexLock lock(&mu_);
  backoff_.Reset();
  if (retry_timer_handle_.has_value() &&
      event_engine_->Cancel(*retry_timer_handle_)) {
    OnRetryTimerLocked();
  } else if (connection_attempt_in_flight_) {
    next_attempt_time_ = Timestamp::Now();
  }
}

void NewSubchannel::Orphaned() {
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString() << ": shutting down";
  // The subchannel_pool is only used once here in this subchannel, so the
  // access can be outside of the lock.
  if (subchannel_pool_ != nullptr) {
    subchannel_pool_->UnregisterSubchannel(key_, this);
    subchannel_pool_.reset();
  }
  MutexLock lock(&mu_);
  GRPC_CHECK(!shutdown_);
  shutdown_ = true;
  connector_.reset();
  connections_.clear();
  if (retry_timer_handle_.has_value()) {
    event_engine_->Cancel(*retry_timer_handle_);
  }
}

void NewSubchannel::GetOrAddDataProducer(
    UniqueTypeName type,
    std::function<void(DataProducerInterface**)> get_or_add) {
  MutexLock lock(&mu_);
  auto it = data_producer_map_.emplace(type, nullptr).first;
  get_or_add(&it->second);
}

void NewSubchannel::RemoveDataProducer(DataProducerInterface* data_producer) {
  MutexLock lock(&mu_);
  auto it = data_producer_map_.find(data_producer->type());
  if (it != data_producer_map_.end() && it->second == data_producer) {
    data_producer_map_.erase(it);
  }
}

namespace {

absl::Status PrependAddressToStatusMessage(const SubchannelKey& key,
                                           const absl::Status& status) {
  return AddMessagePrefix(
      grpc_sockaddr_to_uri(&key.address()).value_or("<unknown address type>"),
      status);
}

}  // namespace

void NewSubchannel::SetLastFailureLocked(const absl::Status& status) {
  // Augment status message to include IP address.
  last_failure_status_ = PrependAddressToStatusMessage(key_, status);
}

grpc_connectivity_state NewSubchannel::ComputeConnectivityStateLocked() const {
  // If we have at least one connection, report READY.
  if (!connections_.empty()) return GRPC_CHANNEL_READY;
  // If we were created from an endpoint and the connection is closed,
  // we have no way to create a new connection, so we report
  // TRANSIENT_FAILURE, and we'll never leave that state.
  if (created_from_endpoint_) return GRPC_CHANNEL_TRANSIENT_FAILURE;
  // If there's a connection attempt in flight, report CONNECTING.
  if (connection_attempt_in_flight_) return GRPC_CHANNEL_CONNECTING;
  // If we're in backoff delay, report TRANSIENT_FAILURE.
  if (retry_timer_handle_.has_value()) {
    return GRPC_CHANNEL_TRANSIENT_FAILURE;
  }
  // Otherwise, report IDLE.
  return GRPC_CHANNEL_IDLE;
}

absl::Status NewSubchannel::ConnectivityStatusToReportLocked() const {
  // Report status in TRANSIENT_FAILURE state.
  if (state_ == GRPC_CHANNEL_TRANSIENT_FAILURE) return last_failure_status_;
  return absl::OkStatus();
}

void NewSubchannel::MaybeUpdateConnectivityStateLocked() {
  // Determine what state we are in.
  grpc_connectivity_state new_state = ComputeConnectivityStateLocked();
  // If we're already in that state, no need to report a change.
  if (new_state == state_) return;
  state_ = new_state;
  absl::Status status = ConnectivityStatusToReportLocked();
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": reporting connectivity state " << ConnectivityStateName(new_state)
      << ", status: " << status;
  // Update channelz.
  if (channelz_node_ != nullptr) {
    channelz_node_->UpdateConnectivityState(new_state);
    if (status.ok()) {
      GRPC_CHANNELZ_LOG(channelz_node_)
          << "Subchannel connectivity state changed to "
          << ConnectivityStateName(new_state);
    } else {
      GRPC_CHANNELZ_LOG(channelz_node_)
          << "Subchannel connectivity state changed to "
          << ConnectivityStateName(new_state) << ": " << status;
    }
  }
  // Notify watchers.
  watcher_list_.NotifyLocked(new_state, status);
}

bool NewSubchannel::RemoveConnectionLocked(
    ConnectedSubchannel* connected_subchannel) {
  for (auto it = connections_.begin(); it != connections_.end(); ++it) {
    if (*it == connected_subchannel) {
      GRPC_TRACE_LOG(subchannel, INFO)
          << "subchannel " << this << " " << key_.ToString()
          << ": removing connection " << connected_subchannel;
      connections_.erase(it);
      return true;
    }
  }
  return false;
}

void NewSubchannel::OnRetryTimer() {
  MutexLock lock(&mu_);
  OnRetryTimerLocked();
}

void NewSubchannel::OnRetryTimerLocked() {
  retry_timer_handle_.reset();
  if (shutdown_) return;
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": backoff delay elapsed";
  RetryQueuedRpcsLocked();  // May trigger another connection attempt.
  MaybeUpdateConnectivityStateLocked();
}

void NewSubchannel::StartConnectingLocked() {
  // Set next attempt time.
  const Timestamp now = Timestamp::Now();
  const Timestamp min_deadline = now + min_connect_timeout_;
  next_attempt_time_ = now + backoff_.NextAttemptDelay();
  // Change connectivity state if needed.
  connection_attempt_in_flight_ = true;
  MaybeUpdateConnectivityStateLocked();
  // Start connection attempt.
  SubchannelConnector::Args args;
  args.address = &address_for_connect_;
  args.interested_parties = pollset_set_;
  args.deadline = std::max(next_attempt_time_, min_deadline);
  args.channel_args =
      args_.Set(GRPC_ARG_MAX_CONCURRENT_STREAMS_REJECT_ON_CLIENT, true);
  WeakRef(DEBUG_LOCATION, "Connect").release();  // Ref held by callback.
  connector_->Connect(args, &connecting_result_, &on_connecting_finished_);
}

void NewSubchannel::OnConnectingFinished(void* arg, grpc_error_handle error) {
  WeakRefCountedPtr<NewSubchannel> c(static_cast<NewSubchannel*>(arg));
  {
    MutexLock lock(&c->mu_);
    c->OnConnectingFinishedLocked(error);
  }
  c.reset(DEBUG_LOCATION, "Connect");
}

void NewSubchannel::OnConnectingFinishedLocked(grpc_error_handle error) {
  connection_attempt_in_flight_ = false;
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
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << this << " " << key_.ToString()
        << ": connect failed (" << StatusToString(error) << ")"
        << (created_from_endpoint_
                ? ", no retry will be attempted (created from endpoint); "
                  "remaining in TRANSIENT_FAILURE"
                : ", backing off for " +
                      std::to_string(time_until_next_attempt.millis()) + " ms");
    if (!created_from_endpoint_) {
      retry_timer_handle_ = event_engine_->RunAfter(
          time_until_next_attempt,
          [self = WeakRef(DEBUG_LOCATION, "RetryTimer")
                      .TakeAsSubclass<NewSubchannel>()]() mutable {
            {
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
    SetLastFailureLocked(grpc_error_to_absl_status(error));
    MaybeUpdateConnectivityStateLocked();
  }
}

bool NewSubchannel::PublishTransportLocked() {
  auto socket_node = connecting_result_.transport->GetSocketNode();
  Transport* transport = connecting_result_.transport;
  RefCountedPtr<ConnectedSubchannel> connected_subchannel;
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
    connected_subchannel = MakeRefCounted<LegacyConnectedSubchannel>(
        WeakRef().TakeAsSubclass<NewSubchannel>(), std::move(*stack), args_,
        channelz_node_, connecting_result_.max_concurrent_streams);
  } else {
    OrphanablePtr<ClientTransport> transport(
        std::exchange(connecting_result_.transport, nullptr)
            ->client_transport());
    absl::Status status;
    connected_subchannel = MakeRefCounted<NewConnectedSubchannel>(
        WeakRef().TakeAsSubclass<NewSubchannel>(), std::move(transport), args_,
        connecting_result_.max_concurrent_streams, &status);
    if (!status.ok()) {
      connecting_result_.Reset();
      LOG(ERROR) << "subchannel " << this << " " << key_.ToString()
                 << ": error initializing subchannel stack: " << status;
      return false;
    }
  }
  connecting_result_.Reset();
  // Publish.
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": new connected subchannel at " << connected_subchannel.get()
      << ", max_concurrent_streams="
      << connecting_result_.max_concurrent_streams;
  if (channelz_node_ != nullptr) {
    if (socket_node != nullptr) {
      socket_node->AddParent(channelz_node_.get());
    }
  }
  transport->StartWatch(
      MakeRefCounted<ConnectionStateWatcher>(connected_subchannel->WeakRef()));
  connections_.push_back(std::move(connected_subchannel));
  RetryQueuedRpcsLocked();
  MaybeUpdateConnectivityStateLocked();
  return true;
}

RefCountedPtr<Subchannel::Call> NewSubchannel::CreateCall(
    CreateCallArgs args, grpc_error_handle* error) {
  RefCountedPtr<ConnectedSubchannel> connected_subchannel;
  {
    MutexLock lock(&mu_);
    // If we hit a race condition where the LB picker chose the subchannel
    // at the same time as the last connection was closed, then tell the
    // channel to re-queue the pick.
    if (connections_.empty()) return nullptr;
    // Otherwise, choose a connection.
    // Optimization: If the queue is non-empty, then we know there won't be
    // a connection that we can send this RPC on, so we don't bother looking.
    if (queued_calls_.empty()) connected_subchannel = ChooseConnectionLocked();
    // If we don't have a connection to send the RPC on, queue it.
    if (connected_subchannel == nullptr) {
      // The QueuedCall object adds itself to queued_calls_.
      auto queued_call = RefCountedPtr<QueuedCall>(args.arena->New<QueuedCall>(
          WeakRef().TakeAsSubclass<NewSubchannel>(), args));
      MaybeFailAllQueuedRpcsLocked();
      return queued_call;
    }
  }
  // Found a connection, so create a call on it.
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": creating call on connection " << connected_subchannel.get();
  return connected_subchannel->CreateCall(args, error);
}

RefCountedPtr<UnstartedCallDestination> NewSubchannel::call_destination() {
  RefCountedPtr<ConnectedSubchannel> connected_subchannel;
  {
    MutexLock lock(&mu_);
    // If we hit a race condition where the LB picker chose the subchannel
    // at the same time as the last connection was closed, then tell the
    // channel to re-queue the pick.
    if (connections_.empty()) return nullptr;
    // Otherwise, choose a connection.
    // Optimization: If the queue is non-empty, then we know there won't be
    // a connection that we can send this RPC on, so we don't bother looking.
    if (queued_calls_.empty()) connected_subchannel = ChooseConnectionLocked();
    // If we don't have a connection to send the RPC on, queue it.
    if (connected_subchannel == nullptr) {
      // The QueuingUnstartedCallDestination object adds itself to
      // queued_calls_.
      return RefCountedPtr<QueuingUnstartedCallDestination>(
          GetContext<Arena>()->New<QueuingUnstartedCallDestination>(
              WeakRef().TakeAsSubclass<NewSubchannel>()));
    }
  }
  // Found a connection, so use it.
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": creating call on connection " << connected_subchannel.get();
  return connected_subchannel->unstarted_call_destination();
}

RefCountedPtr<NewSubchannel::ConnectedSubchannel>
NewSubchannel::ChooseConnectionLocked() {
  // Try to find a connection with quota available for the RPC.
  for (auto& connection : connections_) {
    if (connection->GetQuotaForRpc()) return connection;
  }
  // If we didn't find a connection for the RPC, we'll queue it.
  // Trigger a new connection attempt if we need to scale up the number
  // of connections.
  if (connections_.size() < watcher_list_.GetMaxConnectionsPerSubchannel() &&
      !connection_attempt_in_flight_ && !retry_timer_handle_.has_value()) {
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << this << " " << key_.ToString()
        << ": adding a new connection";
    StartConnectingLocked();
  }
  return nullptr;
}

void NewSubchannel::RetryQueuedRpcs() {
  MutexLock lock(&mu_);
  RetryQueuedRpcsLocked();
}

void NewSubchannel::RetryQueuedRpcsLocked() {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": retrying RPCs from queue, queue size=" << queued_calls_.size();
  while (!queued_calls_.empty()) {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "  retrying first queued RPC, queue size=" << queued_calls_.size();
    QueuedCallInterface* queued_call = queued_calls_.front();
    if (queued_call == nullptr) {
      GRPC_TRACE_LOG(subchannel_call, INFO) << "  RPC already cancelled";
    } else {
      auto connected_subchannel = ChooseConnectionLocked();
      // If we don't have a connection to dispatch this RPC on, then
      // we've drained as much from the queue as we can, so stop here.
      if (connected_subchannel == nullptr) {
        GRPC_TRACE_LOG(subchannel_call, INFO)
            << "  no usable connection found; will stop retrying from queue";
        MaybeFailAllQueuedRpcsLocked();
        return;
      }
      GRPC_TRACE_LOG(subchannel_call, INFO)
          << "  starting RPC on connection " << connected_subchannel.get();
      queued_call->ResumeOnConnectionLocked(connected_subchannel.get());
    }
    queued_calls_.pop_front();
  }
}

void NewSubchannel::MaybeFailAllQueuedRpcsLocked() {
  bool fail_instead_of_queuing =
      args_.GetInt(GRPC_ARG_MAX_CONCURRENT_STREAMS_REJECT_ON_CLIENT)
          .value_or(false);
  if (fail_instead_of_queuing &&
      connections_.size() == watcher_list_.GetMaxConnectionsPerSubchannel()) {
    FailAllQueuedRpcsLocked(
        absl::ResourceExhaustedError("subchannel at max number of connections, "
                                     "but no quota to send RPC"));
  }
}

void NewSubchannel::FailAllQueuedRpcsLocked(absl::Status status) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << this << ": failing all queued RPCs: " << status;
  status = PrependAddressToStatusMessage(key_, status);
  for (QueuedCallInterface* queued_call : queued_calls_) {
    if (queued_call != nullptr) queued_call->FailLocked(status);
  }
  queued_calls_.clear();
}

void NewSubchannel::Ping(absl::AnyInvocable<void(absl::Status)>) {
  // TODO(ctiller): Implement
}

absl::Status NewSubchannel::Ping(grpc_closure* on_initiate,
                                 grpc_closure* on_ack) {
  RefCountedPtr<ConnectedSubchannel> connected_subchannel;
  {
    MutexLock lock(&mu_);
    if (!connections_.empty()) connected_subchannel = connections_[0];
  }
  if (connected_subchannel == nullptr) {
    return absl::UnavailableError("no connection");
  }
  connected_subchannel->Ping(on_initiate, on_ack);
  return absl::OkStatus();
}

}  // namespace grpc_core
