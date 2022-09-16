// Copyright 2022 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
#define GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H

// Scaffolding to allow the per-call part of a filter to be authored in a
// promise-style. Most of this will be removed once the promises conversion is
// completed.

#include <grpc/support/port_platform.h>

#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <new>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/meta/type_traits.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {

class ChannelFilter {
 public:
  class Args {
   public:
    Args() : Args(nullptr, nullptr) {}
    explicit Args(grpc_channel_stack* channel_stack,
                  grpc_channel_element* channel_element)
        : channel_stack_(channel_stack), channel_element_(channel_element) {}

    grpc_channel_stack* channel_stack() const { return channel_stack_; }
    grpc_channel_element* uninitialized_channel_element() {
      return channel_element_;
    }

   private:
    friend class ChannelFilter;
    grpc_channel_stack* channel_stack_;
    grpc_channel_element* channel_element_;
  };

  // Perform post-initialization step (if any).
  virtual void PostInit() {}

  // Construct a promise for one call.
  virtual ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs call_args, NextPromiseFactory next_promise_factory) = 0;

  // Start a legacy transport op
  // Return true if the op was handled, false if it should be passed to the
  // next filter.
  // TODO(ctiller): design a new API for this - we probably don't want big op
  // structures going forward.
  virtual bool StartTransportOp(grpc_transport_op*) { return false; }

  // Perform a legacy get info call
  // Return true if the op was handled, false if it should be passed to the
  // next filter.
  // TODO(ctiller): design a new API for this
  virtual bool GetChannelInfo(const grpc_channel_info*) { return false; }

  virtual ~ChannelFilter() = default;
};

// Designator for whether a filter is client side or server side.
// Please don't use this outside calls to MakePromiseBasedFilter - it's
// intended to be deleted once the promise conversion is complete.
enum class FilterEndpoint {
  kClient,
  kServer,
};

// Flags for MakePromiseBasedFilter.
static constexpr uint8_t kFilterExaminesServerInitialMetadata = 1;
static constexpr uint8_t kFilterIsLast = 2;

namespace promise_filter_detail {

// Proxy channel filter for initialization failure, since we must leave a
// valid filter in place.
class InvalidChannelFilter : public ChannelFilter {
 public:
  ArenaPromise<ServerMetadataHandle> MakeCallPromise(
      CallArgs, NextPromiseFactory) override {
    abort();
  }
};

// Call data shared between all implementations of promise-based filters.
class BaseCallData : public Activity, private Wakeable {
 public:
  BaseCallData(grpc_call_element* elem, const grpc_call_element_args* args,
               uint8_t flags);
  ~BaseCallData() override;

  void set_pollent(grpc_polling_entity* pollent) {
    GPR_ASSERT(nullptr ==
               pollent_.exchange(pollent, std::memory_order_release));
  }

  // Activity implementation (partial).
  void Orphan() final;
  Waker MakeNonOwningWaker() final;
  Waker MakeOwningWaker() final;

  void Finalize(const grpc_call_final_info* final_info) {
    finalization_.Run(final_info);
  }

 protected:
  class ScopedContext
      : public promise_detail::Context<Arena>,
        public promise_detail::Context<grpc_call_context_element>,
        public promise_detail::Context<grpc_polling_entity>,
        public promise_detail::Context<CallFinalization> {
   public:
    explicit ScopedContext(BaseCallData* call_data)
        : promise_detail::Context<Arena>(call_data->arena_),
          promise_detail::Context<grpc_call_context_element>(
              call_data->context_),
          promise_detail::Context<grpc_polling_entity>(
              call_data->pollent_.load(std::memory_order_acquire)),
          promise_detail::Context<CallFinalization>(&call_data->finalization_) {
    }
  };

  class Flusher {
   public:
    explicit Flusher(BaseCallData* call);
    // Calls closures, schedules batches, relinquishes call combiner.
    ~Flusher();

    void Resume(grpc_transport_stream_op_batch* batch) {
      GPR_ASSERT(!call_->is_last());
      release_.push_back(batch);
    }

    void Cancel(grpc_transport_stream_op_batch* batch,
                grpc_error_handle error) {
      grpc_transport_stream_op_batch_queue_finish_with_failure(batch, error,
                                                               &call_closures_);
    }

    void Complete(grpc_transport_stream_op_batch* batch) {
      call_closures_.Add(batch->on_complete, GRPC_ERROR_NONE,
                         "Flusher::Complete");
    }

    void AddClosure(grpc_closure* closure, grpc_error_handle error,
                    const char* reason) {
      call_closures_.Add(closure, error, reason);
    }

   private:
    absl::InlinedVector<grpc_transport_stream_op_batch*, 1> release_;
    CallCombinerClosureList call_closures_;
    BaseCallData* const call_;
  };

  // Smart pointer like wrapper around a batch.
  // Creation makes a ref count of one capture.
  // Copying increments.
  // Must be moved from or resumed or cancelled before destruction.
  class CapturedBatch final {
   public:
    CapturedBatch();
    explicit CapturedBatch(grpc_transport_stream_op_batch* batch);
    ~CapturedBatch();
    CapturedBatch(const CapturedBatch&);
    CapturedBatch& operator=(const CapturedBatch&);
    CapturedBatch(CapturedBatch&&) noexcept;
    CapturedBatch& operator=(CapturedBatch&&) noexcept;

    grpc_transport_stream_op_batch* operator->() { return batch_; }
    bool is_captured() const { return batch_ != nullptr; }

    // Resume processing this batch (releases one ref, passes it down the
    // stack)
    void ResumeWith(Flusher* releaser);
    // Cancel this batch immediately (releases all refs)
    void CancelWith(grpc_error_handle error, Flusher* releaser);
    // Complete this batch (pass it up) assuming refs drop to zero
    void CompleteWith(Flusher* releaser);

    void Swap(CapturedBatch* other) { std::swap(batch_, other->batch_); }

   private:
    grpc_transport_stream_op_batch* batch_;
  };

  static MetadataHandle<grpc_metadata_batch> WrapMetadata(
      grpc_metadata_batch* p) {
    return MetadataHandle<grpc_metadata_batch>(p);
  }

  static grpc_metadata_batch* UnwrapMetadata(
      MetadataHandle<grpc_metadata_batch> p) {
    return p.Unwrap();
  }

  Arena* arena() { return arena_; }
  grpc_call_element* elem() const { return elem_; }
  CallCombiner* call_combiner() const { return call_combiner_; }
  Timestamp deadline() const { return deadline_; }
  grpc_call_stack* call_stack() const { return call_stack_; }
  Latch<ServerMetadata*>* server_initial_metadata_latch() const {
    return server_initial_metadata_latch_;
  }

  bool is_last() const {
    return grpc_call_stack_element(call_stack_, call_stack_->count - 1) ==
           elem_;
  }

 private:
  // Wakeable implementation.
  void Wakeup() final;
  void Drop() final;

  virtual void OnWakeup() = 0;

  grpc_call_stack* const call_stack_;
  grpc_call_element* const elem_;
  Arena* const arena_;
  CallCombiner* const call_combiner_;
  const Timestamp deadline_;
  CallFinalization finalization_;
  grpc_call_context_element* const context_;
  std::atomic<grpc_polling_entity*> pollent_{nullptr};
  Latch<ServerMetadata*>* server_initial_metadata_latch_ = nullptr;
};

class ClientCallData : public BaseCallData {
 public:
  ClientCallData(grpc_call_element* elem, const grpc_call_element_args* args,
                 uint8_t flags);
  ~ClientCallData() override;

  // Activity implementation.
  void ForceImmediateRepoll() final;
  // Handle one grpc_transport_stream_op_batch
  void StartBatch(grpc_transport_stream_op_batch* batch);

 private:
  // At what stage is our handling of send initial metadata?
  enum class SendInitialState {
    // Start state: no op seen
    kInitial,
    // We've seen the op, and started the promise in response to it, but have
    // not yet sent the op to the next filter.
    kQueued,
    // We've sent the op to the next filter.
    kForwarded,
    // We were cancelled.
    kCancelled
  };
  // At what stage is our handling of recv trailing metadata?
  enum class RecvTrailingState {
    // Start state: no op seen
    kInitial,
    // We saw the op, and since it was bundled with send initial metadata, we
    // queued it until the send initial metadata can be sent to the next
    // filter.
    kQueued,
    // We've forwarded the op to the next filter.
    kForwarded,
    // The op has completed from below, but we haven't yet forwarded it up (the
    // promise gets to interject and mutate it).
    kComplete,
    // We've called the recv_metadata_ready callback from the original
    // recv_trailing_metadata op that was presented to us.
    kResponded,
    // We've been cancelled and handled that locally.
    // (i.e. whilst the recv_trailing_metadata op is queued in this filter).
    kCancelled
  };

  struct RecvInitialMetadata;
  class PollContext;

  // Handle cancellation.
  void Cancel(grpc_error_handle error);
  // Begin running the promise - which will ultimately take some initial
  // metadata and return some trailing metadata.
  void StartPromise(Flusher* flusher);
  // Interject our callback into the op batch for recv trailing metadata
  // ready. Stash a pointer to the trailing metadata that will be filled in,
  // so we can manipulate it later.
  void HookRecvTrailingMetadata(CapturedBatch batch);
  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch to be sent down.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<ServerMetadataHandle> MakeNextPromise(CallArgs call_args);
  // Wrapper to make it look like we're calling the next filter as a promise.
  // First poll: send the send_initial_metadata op down the stack.
  // All polls: await receiving the trailing metadata, then return it to the
  // application.
  Poll<ServerMetadataHandle> PollTrailingMetadata();
  static void RecvTrailingMetadataReadyCallback(void* arg,
                                                grpc_error_handle error);
  void RecvTrailingMetadataReady(grpc_error_handle error);
  void RecvInitialMetadataReady(grpc_error_handle error);
  // Given an error, fill in ServerMetadataHandle to represent that error.
  void SetStatusFromError(grpc_metadata_batch* metadata,
                          grpc_error_handle error);
  // Wakeup and poll the promise if appropriate.
  void WakeInsideCombiner(Flusher* flusher);
  void OnWakeup() override;

  // Contained promise
  ArenaPromise<ServerMetadataHandle> promise_;
  // Queued batch containing at least a send_initial_metadata op.
  CapturedBatch send_initial_metadata_batch_;
  // Pointer to where trailing metadata will be stored.
  grpc_metadata_batch* recv_trailing_metadata_ = nullptr;
  // State tracking recv initial metadata for filters that care about it.
  RecvInitialMetadata* recv_initial_metadata_ = nullptr;
  // Closure to call when we're done with the trailing metadata.
  grpc_closure* original_recv_trailing_metadata_ready_ = nullptr;
  // Our closure pointing to RecvTrailingMetadataReadyCallback.
  grpc_closure recv_trailing_metadata_ready_;
  // Error received during cancellation.
  grpc_error_handle cancelled_error_ = GRPC_ERROR_NONE;
  // State of the send_initial_metadata op.
  SendInitialState send_initial_state_ = SendInitialState::kInitial;
  // State of the recv_trailing_metadata op.
  RecvTrailingState recv_trailing_state_ = RecvTrailingState::kInitial;
  // Polling related data. Non-null if we're actively polling
  PollContext* poll_ctx_ = nullptr;
};

class ServerCallData : public BaseCallData {
 public:
  ServerCallData(grpc_call_element* elem, const grpc_call_element_args* args,
                 uint8_t flags);
  ~ServerCallData() override;

  // Activity implementation.
  void ForceImmediateRepoll() final;
  // Handle one grpc_transport_stream_op_batch
  void StartBatch(grpc_transport_stream_op_batch* batch);

 private:
  // At what stage is our handling of recv initial metadata?
  enum class RecvInitialState {
    // Start state: no op seen
    kInitial,
    // Op seen, and forwarded to the next filter.
    // Now waiting for the callback.
    kForwarded,
    // The op has completed from below, but we haven't yet forwarded it up
    // (the promise gets to interject and mutate it).
    kComplete,
    // We've sent the response to the next filter up.
    kResponded,
  };
  // At what stage is our handling of send trailing metadata?
  enum class SendTrailingState {
    // Start state: no op seen
    kInitial,
    // We saw the op, and are waiting for the promise to complete
    // to forward it.
    kQueued,
    // We've forwarded the op to the next filter.
    kForwarded,
    // We were cancelled.
    kCancelled
  };

  class PollContext;
  struct SendInitialMetadata;

  // Handle cancellation.
  void Cancel(grpc_error_handle error, Flusher* flusher);
  // Construct a promise that will "call" the next filter.
  // Effectively:
  //   - put the modified initial metadata into the batch being sent up.
  //   - return a wrapper around PollTrailingMetadata as the promise.
  ArenaPromise<ServerMetadataHandle> MakeNextPromise(CallArgs call_args);
  // Wrapper to make it look like we're calling the next filter as a promise.
  // All polls: await sending the trailing metadata, then foward it down the
  // stack.
  Poll<ServerMetadataHandle> PollTrailingMetadata();
  static void RecvInitialMetadataReadyCallback(void* arg,
                                               grpc_error_handle error);
  void RecvInitialMetadataReady(grpc_error_handle error);
  // Wakeup and poll the promise if appropriate.
  void WakeInsideCombiner(Flusher* flusher);
  void OnWakeup() override;

  // Contained promise
  ArenaPromise<ServerMetadataHandle> promise_;
  // Pointer to where initial metadata will be stored.
  grpc_metadata_batch* recv_initial_metadata_ = nullptr;
  // State for sending initial metadata.
  SendInitialMetadata* send_initial_metadata_ = nullptr;
  // Closure to call when we're done with the trailing metadata.
  grpc_closure* original_recv_initial_metadata_ready_ = nullptr;
  // Our closure pointing to RecvInitialMetadataReadyCallback.
  grpc_closure recv_initial_metadata_ready_;
  // Error received during cancellation.
  grpc_error_handle cancelled_error_ = GRPC_ERROR_NONE;
  // Trailing metadata batch
  CapturedBatch send_trailing_metadata_batch_;
  // State of the send_initial_metadata op.
  RecvInitialState recv_initial_state_ = RecvInitialState::kInitial;
  // State of the recv_trailing_metadata op.
  SendTrailingState send_trailing_state_ = SendTrailingState::kInitial;
  // Current poll context (or nullptr if not polling).
  PollContext* poll_ctx_ = nullptr;
  // Whether to forward the recv_initial_metadata op at the end of promise
  // wakeup.
  bool forward_recv_initial_metadata_callback_ = false;
};

// Specific call data per channel filter.
// Note that we further specialize for clients and servers since their
// implementations are very different.
template <class ChannelFilter, FilterEndpoint endpoint>
class CallData;

// Client implementation of call data.
template <class ChannelFilter>
class CallData<ChannelFilter, FilterEndpoint::kClient> : public ClientCallData {
 public:
  using ClientCallData::ClientCallData;
};

// Server implementation of call data.
template <class ChannelFilter>
class CallData<ChannelFilter, FilterEndpoint::kServer> : public ServerCallData {
 public:
  using ServerCallData::ServerCallData;
};

}  // namespace promise_filter_detail

// F implements ChannelFilter and :
// class SomeChannelFilter : public ChannelFilter {
//  public:
//   static absl::StatusOr<SomeChannelFilter> Create(
//       ChannelArgs channel_args, ChannelFilter::Args filter_args);
// };
template <typename F, FilterEndpoint kEndpoint, uint8_t kFlags = 0>
absl::enable_if_t<std::is_base_of<ChannelFilter, F>::value, grpc_channel_filter>
MakePromiseBasedFilter(const char* name) {
  using CallData = promise_filter_detail::CallData<F, kEndpoint>;

  return grpc_channel_filter{
      // start_transport_stream_op_batch
      [](grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
        static_cast<CallData*>(elem->call_data)->StartBatch(batch);
      },
      // make_call_promise
      [](grpc_channel_element* elem, CallArgs call_args,
         NextPromiseFactory next_promise_factory) {
        return static_cast<ChannelFilter*>(elem->channel_data)
            ->MakeCallPromise(std::move(call_args),
                              std::move(next_promise_factory));
      },
      // start_transport_op
      [](grpc_channel_element* elem, grpc_transport_op* op) {
        if (!static_cast<ChannelFilter*>(elem->channel_data)
                 ->StartTransportOp(op)) {
          grpc_channel_next_op(elem, op);
        }
      },
      // sizeof_call_data
      sizeof(CallData),
      // init_call_elem
      [](grpc_call_element* elem, const grpc_call_element_args* args) {
        new (elem->call_data) CallData(elem, args, kFlags);
        return GRPC_ERROR_NONE;
      },
      // set_pollset_or_pollset_set
      [](grpc_call_element* elem, grpc_polling_entity* pollent) {
        static_cast<CallData*>(elem->call_data)->set_pollent(pollent);
      },
      // destroy_call_elem
      [](grpc_call_element* elem, const grpc_call_final_info* final_info,
         grpc_closure* then_schedule_closure) {
        auto* cd = static_cast<CallData*>(elem->call_data);
        cd->Finalize(final_info);
        cd->~CallData();
        if ((kFlags & kFilterIsLast) != 0) {
          ExecCtx::Run(DEBUG_LOCATION, then_schedule_closure, GRPC_ERROR_NONE);
        } else {
          GPR_ASSERT(then_schedule_closure == nullptr);
        }
      },
      // sizeof_channel_data
      sizeof(F),
      // init_channel_elem
      [](grpc_channel_element* elem, grpc_channel_element_args* args) {
        GPR_ASSERT(args->is_last == ((kFlags & kFilterIsLast) != 0));
        auto status = F::Create(ChannelArgs::FromC(args->channel_args),
                                ChannelFilter::Args(args->channel_stack, elem));
        if (!status.ok()) {
          static_assert(
              sizeof(promise_filter_detail::InvalidChannelFilter) <= sizeof(F),
              "InvalidChannelFilter must fit in F");
          new (elem->channel_data)
              promise_filter_detail::InvalidChannelFilter();
          return absl_status_to_grpc_error(status.status());
        }
        new (elem->channel_data) F(std::move(*status));
        return GRPC_ERROR_NONE;
      },
      // post_init_channel_elem
      [](grpc_channel_stack*, grpc_channel_element* elem) {
        static_cast<ChannelFilter*>(elem->channel_data)->PostInit();
      },
      // destroy_channel_elem
      [](grpc_channel_element* elem) {
        static_cast<ChannelFilter*>(elem->channel_data)->~ChannelFilter();
      },
      // get_channel_info
      [](grpc_channel_element* elem, const grpc_channel_info* info) {
        if (!static_cast<ChannelFilter*>(elem->channel_data)
                 ->GetChannelInfo(info)) {
          grpc_channel_next_get_info(elem, info);
        }
      },
      // name
      name,
  };
}

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_CHANNEL_PROMISE_BASED_FILTER_H
