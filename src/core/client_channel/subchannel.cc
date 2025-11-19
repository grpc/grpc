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
// Subchannel::ConnectedSubchannel
//

class Subchannel::ConnectedSubchannel
    : public DualRefCounted<ConnectedSubchannel> {
 public:
  ~ConnectedSubchannel() override {
    subchannel_.reset(DEBUG_LOCATION, "ConnectedSubchannel");
  }

  const ChannelArgs& args() const { return args_; }
  Subchannel* subchannel() const { return subchannel_.get(); }

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
  bool SetMaxConcurrentStreams(uint32_t max_concurrent_streams) {
    uint64_t prev_stream_counts =
        stream_counts_.load(std::memory_order_acquire);
    uint32_t rpcs_in_flight;
    do {
      rpcs_in_flight = GetRpcsInFlight(prev_stream_counts);
    } while (!stream_counts_.compare_exchange_weak(
        prev_stream_counts,
        MakeStreamCounts(max_concurrent_streams, rpcs_in_flight),
        std::memory_order_acq_rel, std::memory_order_acquire));
    return rpcs_in_flight < max_concurrent_streams;
  }

  // Returns true if the RPC can start.
  bool GetQuotaForRpc() {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "subchannel " << subchannel_.get() << " connection " << this
        << ": attempting to get quota for an RPC...";
    uint64_t prev_stream_counts =
        stream_counts_.load(std::memory_order_acquire);
    do {
      const uint32_t rpcs_in_flight = GetRpcsInFlight(prev_stream_counts);
      const uint32_t max_concurrent_streams =
          GetMaxConcurrentStreams(prev_stream_counts);
      GRPC_TRACE_LOG(subchannel_call, INFO)
          << "  rpcs_in_flight=" << rpcs_in_flight
          << ", max_concurrent_streams=" << max_concurrent_streams;
      if (rpcs_in_flight == max_concurrent_streams) return false;
    } while (!stream_counts_.compare_exchange_weak(
        prev_stream_counts, prev_stream_counts + MakeStreamCounts(0, 1),
        std::memory_order_acq_rel, std::memory_order_acquire));
    return true;
  }

  // Returns true if this RPC finishing brought the connection below quota.
  bool ReturnQuotaForRpc() {
    const uint64_t prev_stream_counts =
        stream_counts_.fetch_sub(MakeStreamCounts(0, 1));
    return GetRpcsInFlight(prev_stream_counts) ==
           GetMaxConcurrentStreams(prev_stream_counts);
  }

 protected:
  explicit ConnectedSubchannel(WeakRefCountedPtr<Subchannel> subchannel,
                               const ChannelArgs& args,
                               uint32_t max_concurrent_streams)
      : DualRefCounted<ConnectedSubchannel>(
            GRPC_TRACE_FLAG_ENABLED(subchannel_refcount) ? "ConnectedSubchannel"
                                                         : nullptr),
        subchannel_(std::move(subchannel)),
        args_(args),
        stream_counts_(MakeStreamCounts(max_concurrent_streams, 0)) {}

 private:
  // First 32 bits are the MAX_CONCURRENT_STREAMS value reported by
  // the transport.
  // Last 32 bits are the current number of RPCs in flight on the connection.
  static uint64_t MakeStreamCounts(uint32_t max_concurrent_streams,
                                   uint32_t rpcs_in_flight) {
    return (static_cast<uint64_t>(max_concurrent_streams) << 32) +
           static_cast<int64_t>(rpcs_in_flight);
  }
  static uint32_t GetMaxConcurrentStreams(uint64_t stream_counts) {
    return static_cast<uint32_t>(stream_counts >> 32);
  }
  static uint32_t GetRpcsInFlight(uint64_t stream_counts) {
    return static_cast<uint32_t>(stream_counts & 0xffffffffu);
  }

  WeakRefCountedPtr<Subchannel> subchannel_;
  ChannelArgs args_;
  std::atomic<uint64_t> stream_counts_{0};
};

//
// Subchannel::LegacyConnectedSubchannel
//

class Subchannel::LegacyConnectedSubchannel final : public ConnectedSubchannel {
 public:
  LegacyConnectedSubchannel(
      WeakRefCountedPtr<Subchannel> subchannel,
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
// Subchannel::LegacyConnectedSubchannel::SubchannelCall
//

Subchannel::LegacyConnectedSubchannel::SubchannelCall::SubchannelCall(
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

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::
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

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::
    SetAfterCallStackDestroy(grpc_closure* closure) {
  GRPC_CHECK_EQ(after_call_stack_destroy_, nullptr);
  GRPC_CHECK_NE(closure, nullptr);
  after_call_stack_destroy_ = closure;
}

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::Unref() {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::Unref(
    const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_UNREF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::Destroy(
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

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::
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

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::
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

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::MaybeReturnQuota() {
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

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::
    IncrementRefCount() {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), "");
}

void Subchannel::LegacyConnectedSubchannel::SubchannelCall::IncrementRefCount(
    const DebugLocation& /*location*/, const char* reason) {
  GRPC_CALL_STACK_REF(SUBCHANNEL_CALL_TO_CALL_STACK(this), reason);
}

//
// Subchannel::QueuedCall
//

class Subchannel::QueuedCall final : public Subchannel::Call {
 public:
  QueuedCall(WeakRefCountedPtr<Subchannel> subchannel, CreateCallArgs args);
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
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Subchannel::mu_);

  void Fail(absl::Status status)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Subchannel::mu_);

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
  WeakRefCountedPtr<Subchannel> subchannel_;
  CreateCallArgs args_;
  grpc_closure* after_call_stack_destroy_ = nullptr;
  grpc_error_handle cancel_error_;
  Mutex mu_ ABSL_ACQUIRED_AFTER(Subchannel::mu_);
  BufferedCall buffered_call_ ABSL_GUARDED_BY(&mu_);
  RefCountedPtr<Call> subchannel_call_ ABSL_GUARDED_BY(&mu_);
  QueuedCall*& queue_entry_;
  Canceller* canceller_ ABSL_GUARDED_BY(&Subchannel::mu_);

  std::atomic<bool> is_retriable_{false};
  grpc_closure recv_trailing_metadata_ready_;
  grpc_closure* original_recv_trailing_metadata_ = nullptr;
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
};

class Subchannel::QueuedCall::Canceller final {
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

Subchannel::QueuedCall::QueuedCall(WeakRefCountedPtr<Subchannel> subchannel,
                                   CreateCallArgs args)
    : subchannel_(std::move(subchannel)),
      args_(args),
      buffered_call_(args_.call_combiner, &subchannel_call_trace),
      queue_entry_(subchannel_->queued_calls_.emplace_back(this)) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << ": created queued call " << this
      << ", queue size=" << subchannel_->queued_calls_.size();
  canceller_ = new Canceller(Ref().TakeAsSubclass<QueuedCall>());
}

Subchannel::QueuedCall::~QueuedCall() {
  GRPC_TRACE_LOG(subchannel_call, INFO) << "subchannel " << subchannel_.get()
                                        << ": destroying queued call " << this;
  if (after_call_stack_destroy_ != nullptr) {
    ExecCtx::Run(DEBUG_LOCATION, after_call_stack_destroy_, absl::OkStatus());
  }
}

void Subchannel::QueuedCall::SetAfterCallStackDestroy(grpc_closure* closure) {
  GRPC_CHECK_EQ(after_call_stack_destroy_, nullptr);
  GRPC_CHECK_NE(closure, nullptr);
  after_call_stack_destroy_ = closure;
}

void Subchannel::QueuedCall::StartTransportStreamOpBatch(
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
  // We hang on to the call combiner for the send_initial_metadata batch,
  // but yield it for other batches.  This ensures that we are holding on
  // to the call combiner exactly once when we are ready to resume.
  if (!batch->send_initial_metadata) {
    GRPC_CALL_COMBINER_STOP(args_.call_combiner,
                            "batch does not include send_initial_metadata");
  }
}

void Subchannel::QueuedCall::RecvTrailingMetadataReady(
    void* arg, grpc_error_handle error) {
  QueuedCall* call = static_cast<QueuedCall*>(arg);
  GRPC_CHECK_NE(call->recv_trailing_metadata_, nullptr);
  if (call->is_retriable_.load()) {
    call->recv_trailing_metadata_->Set(GrpcStreamNetworkState(),
                                       GrpcStreamNetworkState::kNotSentOnWire);
  }
  Closure::Run(DEBUG_LOCATION, call->original_recv_trailing_metadata_, error);
}

void Subchannel::QueuedCall::ResumeOnConnectionLocked(
    ConnectedSubchannel* connected_subchannel) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << " queued call " << this
      << ": resuming on connected_subchannel " << connected_subchannel;
  canceller_ = nullptr;
  queue_entry_ = nullptr;
  MutexLock lock(&mu_);
  grpc_error_handle error;
  subchannel_call_ = connected_subchannel->CreateCall(args_, &error);
  if (after_call_stack_destroy_ != nullptr) {
    subchannel_call_->SetAfterCallStackDestroy(after_call_stack_destroy_);
    after_call_stack_destroy_ = nullptr;
  }
  if (!error.ok()) {
    buffered_call_.Fail(error,
                        BufferedCall::YieldCallCombinerIfPendingBatchesFound);
  } else {
    buffered_call_.Resume([subchannel_call = subchannel_call_](
                              grpc_transport_stream_op_batch* batch) {
      // This will release the call combiner.
      subchannel_call->StartTransportStreamOpBatch(batch);
    });
  }
}

void Subchannel::QueuedCall::Fail(absl::Status status) {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << subchannel_.get() << " queued call " << this
      << ": failing: " << status;
  canceller_ = nullptr;
  queue_entry_ = nullptr;
  is_retriable_.store(true);
  MutexLock lock(&mu_);
  buffered_call_.Fail(status,
                      BufferedCall::YieldCallCombinerIfPendingBatchesFound);
}

//
// Subchannel::NewConnectedSubchannel
//

class Subchannel::NewConnectedSubchannel final : public ConnectedSubchannel {
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
      WeakRefCountedPtr<Subchannel> subchannel,
      RefCountedPtr<UnstartedCallDestination> call_destination,
      RefCountedPtr<TransportCallDestination> transport,
      const ChannelArgs& args, uint32_t max_concurrent_streams)
      : ConnectedSubchannel(std::move(subchannel), args,
                            max_concurrent_streams),
        call_destination_(std::move(call_destination)),
        transport_(std::move(transport)) {}

  void Orphaned() override {
    call_destination_.reset();
    transport_.reset();
  }

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
// Subchannel::ConnectedSubchannelStateWatcher
//

class Subchannel::ConnectedSubchannelStateWatcher final
    : public AsyncConnectivityStateWatcherInterface {
 public:
  // Must be instantiated while holding c->mu.
  explicit ConnectedSubchannelStateWatcher(
      WeakRefCountedPtr<ConnectedSubchannel> connected_subchannel)
      : connected_subchannel_(std::move(connected_subchannel)) {}

 private:
  void OnConnectivityStateChange(grpc_connectivity_state new_state,
                                 const absl::Status& status) override {
    Subchannel* c = connected_subchannel_->subchannel();
    {
      MutexLock lock(&c->mu_);
      // If we're either shutting down or have already seen this connection
      // failure (i.e., connected_subchannel_ is not present in
      // subchannel_->connections_), do nothing.
      //
      // The transport reports TRANSIENT_FAILURE upon GOAWAY but SHUTDOWN
      // upon connection close.  So if the server gracefully shuts down,
      // we will see TRANSIENT_FAILURE followed by SHUTDOWN, but if not, we
      // will see only SHUTDOWN.  Either way, we react to the first one we
      // see, ignoring anything that happens after that.
      if (new_state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
          new_state == GRPC_CHANNEL_SHUTDOWN) {
        if (!c->RemoveConnectionLocked(connected_subchannel_.get())) return;
        GRPC_TRACE_LOG(subchannel, INFO)
            << "subchannel " << c << " " << c->key_.ToString()
            << ": Connected subchannel " << connected_subchannel_.get()
            << " reports " << ConnectivityStateName(new_state) << ": "
            << status;
        // Record the failure status.
        // Need to do this here to propagate keepalive time.
        c->SetLastFailureLocked(status);
        // If the connectivity state has changed, report the change.
        c->MaybeUpdateConnectivityStateLocked();
        // Reset backoff.
        c->backoff_.Reset();
      }
    }
  }

  WeakRefCountedPtr<ConnectedSubchannel> connected_subchannel_;
};

//
// Subchannel::ConnectionStateWatcher
//

class Subchannel::ConnectionStateWatcher final
    : public Transport::StateWatcher {
 public:
  explicit ConnectionStateWatcher(
      WeakRefCountedPtr<ConnectedSubchannel> connected_subchannel)
      : connected_subchannel_(std::move(connected_subchannel)) {}

  void OnDisconnect(absl::Status status,
                    DisconnectInfo disconnect_info) override {
    Subchannel* subchannel = connected_subchannel_->subchannel();
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
      subchannel->FailAllQueuedRpcsLocked();
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
    Subchannel* subchannel = connected_subchannel_->subchannel();
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
// Subchannel::ConnectivityStateWatcherList
//

void Subchannel::ConnectivityStateWatcherList::AddWatcherLocked(
    RefCountedPtr<ConnectivityStateWatcherInterface> watcher) {
  watchers_.insert(std::move(watcher));
}

void Subchannel::ConnectivityStateWatcherList::RemoveWatcherLocked(
    ConnectivityStateWatcherInterface* watcher) {
  watchers_.erase(watcher);
}

void Subchannel::ConnectivityStateWatcherList::NotifyLocked(
    grpc_connectivity_state state, const absl::Status& status) {
  for (const auto& watcher : watchers_) {
    subchannel_->work_serializer_.Run([watcher, state, status]() {
      watcher->OnConnectivityStateChange(state, status);
    });
  }
}

void Subchannel::ConnectivityStateWatcherList::NotifyOnKeepaliveUpdateLocked(
    Duration new_keepalive_time) {
  for (const auto& watcher : watchers_) {
    subchannel_->work_serializer_.Run([watcher, new_keepalive_time]() {
      watcher->OnKeepaliveUpdate(new_keepalive_time);
    });
  }
}

uint32_t
Subchannel::ConnectivityStateWatcherList::GetMaxConnectionsPerSubchannel()
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
// Subchannel
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

Subchannel::Subchannel(SubchannelKey key,
                       OrphanablePtr<SubchannelConnector> connector,
                       const ChannelArgs& args)
    : DualRefCounted<Subchannel>(GRPC_TRACE_FLAG_ENABLED(subchannel_refcount)
                                     ? "Subchannel"
                                     : nullptr),
      key_(std::move(key)),
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

Subchannel::~Subchannel() {
  if (channelz_node_ != nullptr) {
    GRPC_CHANNELZ_LOG(channelz_node_) << "Subchannel destroyed";
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
  GRPC_CHECK_NE(subchannel_pool, nullptr);
  RefCountedPtr<Subchannel> c = subchannel_pool->FindSubchannel(key);
  if (c != nullptr) {
    return c;
  }
  c = MakeRefCounted<Subchannel>(std::move(key), std::move(connector), args);
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
  RefCountedPtr<Subchannel> registered =
      subchannel_pool->RegisterSubchannel(c->key_, c);
  if (registered == c) c->subchannel_pool_ = subchannel_pool->Ref();
  return registered;
}

void Subchannel::ThrottleKeepaliveTime(Duration new_keepalive_time) {
  MutexLock lock(&mu_);
  ThrottleKeepaliveTimeLocked(new_keepalive_time);
}

void Subchannel::ThrottleKeepaliveTimeLocked(Duration new_keepalive_time) {
  // Only update the value if the new keepalive time is larger.
  if (new_keepalive_time > keepalive_time_) {
    keepalive_time_ = new_keepalive_time;
    GRPC_TRACE_LOG(subchannel, INFO)
        << "subchannel " << this << " " << key_.ToString()
        << ": throttling keepalive time to " << new_keepalive_time;
    args_ = args_.Set(GRPC_ARG_KEEPALIVE_TIME_MS, new_keepalive_time.millis());
  }
}

channelz::SubchannelNode* Subchannel::channelz_node() {
  return channelz_node_.get();
}

void Subchannel::WatchConnectivityState(
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

void Subchannel::CancelConnectivityStateWatch(
    ConnectivityStateWatcherInterface* watcher) {
  MutexLock lock(&mu_);
  grpc_pollset_set* interested_parties = watcher->interested_parties();
  if (interested_parties != nullptr) {
    grpc_pollset_set_del_pollset_set(pollset_set_, interested_parties);
  }
  watcher_list_.RemoveWatcherLocked(watcher);
}

void Subchannel::RequestConnection() {
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": RequestConnection()";
  MutexLock lock(&mu_);
  if (state_ == GRPC_CHANNEL_IDLE) {
    StartConnectingLocked();
  }
}

void Subchannel::ResetBackoff() {
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

void Subchannel::Orphaned() {
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

absl::Status PrependAddressToStatusMessage(const SubchannelKey& key,
                                           const absl::Status& status) {
  return AddMessagePrefix(
      grpc_sockaddr_to_uri(&key.address()).value_or("<unknown address type>"),
      status);
}

}  // namespace

void Subchannel::SetLastFailureLocked(const absl::Status& status) {
  // Augment status message to include IP address.
  last_failure_status_ = PrependAddressToStatusMessage(key_, status);
}

grpc_connectivity_state Subchannel::ComputeConnectivityStateLocked() const {
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

absl::Status Subchannel::ConnectivityStatusToReportLocked() const {
  // Report status in TRANSIENT_FAILURE state.
  // If using the old watcher API, also report status in IDLE state, since
  // that's used to propagate keepalive times.
  if (state_ == GRPC_CHANNEL_TRANSIENT_FAILURE ||
      (!IsSubchannelConnectionScalingEnabled() &&
       state_ == GRPC_CHANNEL_IDLE)) {
    return last_failure_status_;
  }
  return absl::OkStatus();
}

void Subchannel::MaybeUpdateConnectivityStateLocked() {
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

bool Subchannel::RemoveConnectionLocked(
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

void Subchannel::OnRetryTimer() {
  MutexLock lock(&mu_);
  OnRetryTimerLocked();
}

void Subchannel::OnRetryTimerLocked() {
  retry_timer_handle_.reset();
  if (shutdown_) return;
  GRPC_TRACE_LOG(subchannel, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": backoff delay elapsed";
  RetryQueuedRpcsLocked();  // May trigger another connection attempt.
  MaybeUpdateConnectivityStateLocked();
}

void Subchannel::StartConnectingLocked() {
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
          [self = WeakRef(DEBUG_LOCATION, "RetryTimer")]() mutable {
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

bool Subchannel::PublishTransportLocked() {
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
        WeakRef(), std::move(*stack), args_, channelz_node_,
        connecting_result_.max_concurrent_streams);
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
    connected_subchannel = MakeRefCounted<NewConnectedSubchannel>(
        WeakRef(), std::move(*call_destination),
        std::move(transport_destination), args_,
        connecting_result_.max_concurrent_streams);
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
  if (IsSubchannelConnectionScalingEnabled()) {
    transport->StartWatch(MakeRefCounted<ConnectionStateWatcher>(
        connected_subchannel->WeakRef()));
  } else {
    connected_subchannel->StartWatch(
        pollset_set_, MakeOrphanable<ConnectedSubchannelStateWatcher>(
                          connected_subchannel->WeakRef()));
  }
  connections_.push_back(std::move(connected_subchannel));
  RetryQueuedRpcsLocked();
  MaybeUpdateConnectivityStateLocked();
  return true;
}

RefCountedPtr<Subchannel::Call> Subchannel::CreateCall(
    CreateCallArgs args, grpc_error_handle* error) {
  RefCountedPtr<ConnectedSubchannel> connected_subchannel;
  {
    MutexLock lock(&mu_);
    // If we hit a race condition where the LB picker chose the subchannel
    // at the same time as the last connection was closed, then tell the
    // channel to re-queue the pick.
    if (connections_.empty()) return nullptr;
    // Otherwise, choose a connection.
    connected_subchannel = ChooseConnectionLocked();
    // If we don't have a connection to send the RPC on, queue it.
    if (connected_subchannel == nullptr) {
      return RefCountedPtr<QueuedCall>(
          args.arena->New<QueuedCall>(WeakRef(), args));
    }
  }
  // Found a connection, so create a call on it.
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": creating call on connection " << connected_subchannel.get();
  return connected_subchannel->CreateCall(args, error);
}

RefCountedPtr<UnstartedCallDestination> Subchannel::call_destination() {
  // TODO(roth): Implement connection scaling for v3.
  RefCountedPtr<ConnectedSubchannel> connected_subchannel;
  {
    MutexLock lock(&mu_);
    if (!connections_.empty()) connected_subchannel = connections_[0];
  }
  if (connected_subchannel == nullptr) return nullptr;
  return connected_subchannel->unstarted_call_destination();
}

RefCountedPtr<Subchannel::ConnectedSubchannel>
Subchannel::ChooseConnectionLocked() {
  if (!IsSubchannelConnectionScalingEnabled()) return connections_[0];
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

void Subchannel::RetryQueuedRpcs() {
  MutexLock lock(&mu_);
  RetryQueuedRpcsLocked();
}

void Subchannel::RetryQueuedRpcsLocked() {
  GRPC_TRACE_LOG(subchannel_call, INFO)
      << "subchannel " << this << " " << key_.ToString()
      << ": retrying RPCs from queue, queue size=" << queued_calls_.size();
  while (!queued_calls_.empty()) {
    GRPC_TRACE_LOG(subchannel_call, INFO)
        << "  retrying first queued RPC, queue size=" << queued_calls_.size();
    QueuedCall* queued_call = queued_calls_.front();
    if (queued_call == nullptr) {
      GRPC_TRACE_LOG(subchannel_call, INFO) << "  RPC already cancelled";
    } else {
      auto connected_subchannel = ChooseConnectionLocked();
      // If we don't have a connection to dispatch this RPC on, then
      // we've drained as much from the queue as we can, so stop here.
      if (connected_subchannel == nullptr) {
        GRPC_TRACE_LOG(subchannel_call, INFO)
            << "  no usable connection found; will stop retrying from queue";
        return;
      }
      GRPC_TRACE_LOG(subchannel_call, INFO)
          << "  starting RPC on connection " << connected_subchannel.get();
      queued_call->ResumeOnConnectionLocked(connected_subchannel.get());
    }
    queued_calls_.pop_front();
  }
}

void Subchannel::FailAllQueuedRpcsLocked() {
  absl::Status status = PrependAddressToStatusMessage(
      key_, absl::UnavailableError("subchannel lost all connections"));
  for (QueuedCall* queued_call : queued_calls_) {
    if (queued_call != nullptr) queued_call->Fail(status);
  }
  queued_calls_.clear();
}

void Subchannel::Ping(absl::AnyInvocable<void(absl::Status)>) {
  // TODO(ctiller): Implement
}

absl::Status Subchannel::Ping(grpc_closure* on_initiate, grpc_closure* on_ack) {
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

}  // namespace grpc_core
