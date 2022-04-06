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

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/promise_based_filter.h"

#include <cstdlib>

#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {
namespace promise_filter_detail {

///////////////////////////////////////////////////////////////////////////////
// BaseCallData

BaseCallData::BaseCallData(grpc_call_element* elem,
                           const grpc_call_element_args* args, uint8_t flags)
    : call_stack_(args->call_stack),
      elem_(elem),
      arena_(args->arena),
      call_combiner_(args->call_combiner),
      deadline_(args->deadline),
      context_(args->context) {
  if (flags & kFilterExaminesServerInitialMetadata) {
    server_initial_metadata_latch_ = arena_->New<Latch<ServerMetadata*>>();
  }
}

BaseCallData::~BaseCallData() {
  if (server_initial_metadata_latch_ != nullptr) {
    server_initial_metadata_latch_->~Latch();
  }
}

// We don't form ActivityPtr's to this type, and consequently don't need
// Orphan().
void BaseCallData::Orphan() { abort(); }

// For now we don't care about owning/non-owning wakers, instead just share
// implementation.
Waker BaseCallData::MakeNonOwningWaker() { return MakeOwningWaker(); }

Waker BaseCallData::MakeOwningWaker() {
  GRPC_CALL_STACK_REF(call_stack_, "waker");
  return Waker(this);
}

void BaseCallData::Wakeup() {
  auto wakeup = [](void* p, grpc_error_handle) {
    auto* self = static_cast<BaseCallData*>(p);
    self->OnWakeup();
    self->Drop();
  };
  auto* closure = GRPC_CLOSURE_CREATE(wakeup, this, nullptr);
  GRPC_CALL_COMBINER_START(call_combiner_, closure, GRPC_ERROR_NONE, "wakeup");
}

void BaseCallData::Drop() { GRPC_CALL_STACK_UNREF(call_stack_, "waker"); }

///////////////////////////////////////////////////////////////////////////////
// ClientCallData

struct ClientCallData::RecvInitialMetadata final {
  enum State {
    // Initial state; no op seen
    kInitial,
    // No op seen, but we have a latch that would like to modify it when we do
    kGotLatch,
    // Responded to trailing metadata prior to getting a recv_initial_metadata
    kRespondedToTrailingMetadataPriorToHook,
    // Hooked, no latch yet
    kHookedWaitingForLatch,
    // Hooked, latch seen
    kHookedAndGotLatch,
    // Got the callback, haven't set latch yet
    kCompleteWaitingForLatch,
    // Got the callback and got the latch
    kCompleteAndGotLatch,
    // Got the callback and set the latch
    kCompleteAndSetLatch,
    // Called the original callback
    kResponded,
  };

  State state = kInitial;
  grpc_closure* original_on_ready = nullptr;
  grpc_closure on_ready;
  grpc_metadata_batch* metadata = nullptr;
  Latch<ServerMetadata*>* server_initial_metadata_publisher = nullptr;
};

class ClientCallData::PollContext {
 public:
  explicit PollContext(ClientCallData* self) : self_(self) {
    GPR_ASSERT(self_->poll_ctx_ == nullptr);
    self_->poll_ctx_ = this;
    scoped_activity_.Init(self_);
    have_scoped_activity_ = true;
  }

  PollContext(const PollContext&) = delete;
  PollContext& operator=(const PollContext&) = delete;

  void Run() {
    GPR_ASSERT(have_scoped_activity_);
    repoll_ = false;
    if (self_->server_initial_metadata_latch() != nullptr) {
      switch (self_->recv_initial_metadata_->state) {
        case RecvInitialMetadata::kInitial:
        case RecvInitialMetadata::kGotLatch:
        case RecvInitialMetadata::kHookedWaitingForLatch:
        case RecvInitialMetadata::kHookedAndGotLatch:
        case RecvInitialMetadata::kCompleteWaitingForLatch:
        case RecvInitialMetadata::kResponded:
        case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
          break;
        case RecvInitialMetadata::kCompleteAndGotLatch:
          self_->recv_initial_metadata_->state =
              RecvInitialMetadata::kCompleteAndSetLatch;
          self_->recv_initial_metadata_->server_initial_metadata_publisher->Set(
              self_->recv_initial_metadata_->metadata);
          ABSL_FALLTHROUGH_INTENDED;
        case RecvInitialMetadata::kCompleteAndSetLatch: {
          Poll<ServerMetadata**> p =
              self_->server_initial_metadata_latch()->Wait()();
          if (ServerMetadata*** ppp = absl::get_if<ServerMetadata**>(&p)) {
            ServerMetadata* md = **ppp;
            if (self_->recv_initial_metadata_->metadata != md) {
              *self_->recv_initial_metadata_->metadata = std::move(*md);
            }
            self_->recv_initial_metadata_->state =
                RecvInitialMetadata::kResponded;
            call_closures_.Add(
                absl::exchange(self_->recv_initial_metadata_->original_on_ready,
                               nullptr),
                GRPC_ERROR_NONE,
                "wake_inside_combiner:recv_initial_metadata_ready");
          }
        } break;
      }
    }
    if (self_->recv_trailing_state_ == RecvTrailingState::kCancelled ||
        self_->recv_trailing_state_ == RecvTrailingState::kResponded) {
      return;
    }
    switch (self_->send_initial_state_) {
      case SendInitialState::kQueued:
      case SendInitialState::kForwarded: {
        // Poll the promise once since we're waiting for it.
        Poll<ServerMetadataHandle> poll = self_->promise_();
        if (auto* r = absl::get_if<ServerMetadataHandle>(&poll)) {
          auto* md = UnwrapMetadata(std::move(*r));
          bool destroy_md = true;
          if (self_->recv_trailing_state_ == RecvTrailingState::kComplete) {
            if (self_->recv_trailing_metadata_ != md) {
              *self_->recv_trailing_metadata_ = std::move(*md);
            } else {
              destroy_md = false;
            }
            self_->recv_trailing_state_ = RecvTrailingState::kResponded;
            call_closures_.Add(
                absl::exchange(self_->original_recv_trailing_metadata_ready_,
                               nullptr),
                GRPC_ERROR_NONE, "wake_inside_combiner:recv_trailing_ready:1");
            if (self_->recv_initial_metadata_ != nullptr) {
              switch (self_->recv_initial_metadata_->state) {
                case RecvInitialMetadata::kInitial:
                case RecvInitialMetadata::kGotLatch:
                  self_->recv_initial_metadata_->state = RecvInitialMetadata::
                      kRespondedToTrailingMetadataPriorToHook;
                  break;
                case RecvInitialMetadata::
                    kRespondedToTrailingMetadataPriorToHook:
                  abort();  // not reachable
                  break;
                case RecvInitialMetadata::kHookedWaitingForLatch:
                case RecvInitialMetadata::kHookedAndGotLatch:
                case RecvInitialMetadata::kResponded:
                case RecvInitialMetadata::kCompleteAndGotLatch:
                case RecvInitialMetadata::kCompleteAndSetLatch:
                  break;
                case RecvInitialMetadata::kCompleteWaitingForLatch:
                  self_->recv_initial_metadata_->state =
                      RecvInitialMetadata::kResponded;
                  call_closures_.Add(
                      absl::exchange(
                          self_->recv_initial_metadata_->original_on_ready,
                          nullptr),
                      GRPC_ERROR_CANCELLED,
                      "wake_inside_combiner:recv_initial_metadata_ready");
              }
            }
          } else {
            GPR_ASSERT(*md->get_pointer(GrpcStatusMetadata()) !=
                       GRPC_STATUS_OK);
            grpc_error_handle error = grpc_error_set_int(
                GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                    "early return from promise based filter"),
                GRPC_ERROR_INT_GRPC_STATUS,
                *md->get_pointer(GrpcStatusMetadata()));
            if (auto* message = md->get_pointer(GrpcMessageMetadata())) {
              error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                                         message->as_string_view());
            }
            GRPC_ERROR_UNREF(self_->cancelled_error_);
            self_->cancelled_error_ = GRPC_ERROR_REF(error);
            if (self_->recv_initial_metadata_ != nullptr) {
              switch (self_->recv_initial_metadata_->state) {
                case RecvInitialMetadata::kInitial:
                case RecvInitialMetadata::kGotLatch:
                  self_->recv_initial_metadata_->state = RecvInitialMetadata::
                      kRespondedToTrailingMetadataPriorToHook;
                  break;
                case RecvInitialMetadata::kHookedWaitingForLatch:
                case RecvInitialMetadata::kHookedAndGotLatch:
                case RecvInitialMetadata::kResponded:
                  break;
                case RecvInitialMetadata::
                    kRespondedToTrailingMetadataPriorToHook:
                  abort();  // not reachable
                  break;
                case RecvInitialMetadata::kCompleteWaitingForLatch:
                case RecvInitialMetadata::kCompleteAndGotLatch:
                case RecvInitialMetadata::kCompleteAndSetLatch:
                  self_->recv_initial_metadata_->state =
                      RecvInitialMetadata::kResponded;
                  call_closures_.Add(
                      absl::exchange(
                          self_->recv_initial_metadata_->original_on_ready,
                          nullptr),
                      GRPC_ERROR_REF(error),
                      "wake_inside_combiner:recv_initial_metadata_ready");
              }
            }
            if (self_->send_initial_state_ == SendInitialState::kQueued) {
              self_->send_initial_state_ = SendInitialState::kCancelled;
              cancel_send_initial_metadata_error_ = error;
            } else {
              GPR_ASSERT(
                  self_->recv_trailing_state_ == RecvTrailingState::kInitial ||
                  self_->recv_trailing_state_ == RecvTrailingState::kForwarded);
              self_->call_combiner()->Cancel(GRPC_ERROR_REF(error));
              forward_batch_ =
                  grpc_make_transport_stream_op(GRPC_CLOSURE_CREATE(
                      [](void* p, grpc_error_handle) {
                        GRPC_CALL_COMBINER_STOP(static_cast<CallCombiner*>(p),
                                                "finish_cancel");
                      },
                      self_->call_combiner(), nullptr));
              forward_batch_->cancel_stream = true;
              forward_batch_->payload->cancel_stream.cancel_error = error;
            }
            self_->recv_trailing_state_ = RecvTrailingState::kCancelled;
          }
          if (destroy_md) {
            md->~grpc_metadata_batch();
          }
          scoped_activity_.Destroy();
          have_scoped_activity_ = false;
          self_->promise_ = ArenaPromise<ServerMetadataHandle>();
        }
      } break;
      case SendInitialState::kInitial:
      case SendInitialState::kCancelled:
        // If we get a response without sending anything, we just propagate
        // that up. (note: that situation isn't possible once we finish the
        // promise transition).
        if (self_->recv_trailing_state_ == RecvTrailingState::kComplete) {
          self_->recv_trailing_state_ = RecvTrailingState::kResponded;
          call_closures_.Add(
              absl::exchange(self_->original_recv_trailing_metadata_ready_,
                             nullptr),
              GRPC_ERROR_NONE, "wake_inside_combiner:recv_trailing_ready:2");
        }
        break;
    }
  }

  ~PollContext() {
    self_->poll_ctx_ = nullptr;
    if (have_scoped_activity_) scoped_activity_.Destroy();
    GRPC_CALL_STACK_REF(self_->call_stack(), "finish_poll");
    bool in_combiner = true;
    if (call_closures_.size() != 0) {
      if (forward_batch_ != nullptr) {
        call_closures_.RunClosuresWithoutYielding(self_->call_combiner());
      } else {
        in_combiner = false;
        call_closures_.RunClosures(self_->call_combiner());
      }
    }
    if (forward_batch_ != nullptr) {
      GPR_ASSERT(in_combiner);
      in_combiner = false;
      forward_send_initial_metadata_ = false;
      grpc_call_next_op(self_->elem(), forward_batch_);
    }
    if (cancel_send_initial_metadata_error_ != GRPC_ERROR_NONE) {
      GPR_ASSERT(in_combiner);
      forward_send_initial_metadata_ = false;
      in_combiner = false;
      grpc_transport_stream_op_batch_finish_with_failure(
          absl::exchange(self_->send_initial_metadata_batch_, nullptr),
          cancel_send_initial_metadata_error_, self_->call_combiner());
    }
    if (absl::exchange(forward_send_initial_metadata_, false)) {
      GPR_ASSERT(in_combiner);
      in_combiner = false;
      grpc_call_next_op(
          self_->elem(),
          absl::exchange(self_->send_initial_metadata_batch_, nullptr));
    }
    if (repoll_) {
      if (in_combiner) {
        self_->WakeInsideCombiner();
      } else {
        struct NextPoll : public grpc_closure {
          grpc_call_stack* call_stack;
          ClientCallData* call_data;
        };
        auto run = [](void* p, grpc_error_handle) {
          auto* next_poll = static_cast<NextPoll*>(p);
          next_poll->call_data->WakeInsideCombiner();
          GRPC_CALL_STACK_UNREF(next_poll->call_stack, "re-poll");
          delete next_poll;
        };
        auto* p = absl::make_unique<NextPoll>().release();
        p->call_stack = self_->call_stack();
        p->call_data = self_;
        GRPC_CALL_STACK_REF(self_->call_stack(), "re-poll");
        GRPC_CLOSURE_INIT(p, run, p, nullptr);
        GRPC_CALL_COMBINER_START(self_->call_combiner(), p, GRPC_ERROR_NONE,
                                 "re-poll");
      }
    } else if (in_combiner) {
      GRPC_CALL_COMBINER_STOP(self_->call_combiner(), "poll paused");
    }
    GRPC_CALL_STACK_UNREF(self_->call_stack(), "finish_poll");
  }

  void Repoll() { repoll_ = true; }

  void ForwardSendInitialMetadata() { forward_send_initial_metadata_ = true; }

 private:
  ManualConstructor<ScopedActivity> scoped_activity_;
  ClientCallData* self_;
  CallCombinerClosureList call_closures_;
  grpc_error_handle cancel_send_initial_metadata_error_ = GRPC_ERROR_NONE;
  grpc_transport_stream_op_batch* forward_batch_ = nullptr;
  bool repoll_ = false;
  bool forward_send_initial_metadata_ = false;
  bool have_scoped_activity_;
};

ClientCallData::ClientCallData(grpc_call_element* elem,
                               const grpc_call_element_args* args,
                               uint8_t flags)
    : BaseCallData(elem, args, flags) {
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
  if (server_initial_metadata_latch() != nullptr) {
    recv_initial_metadata_ = arena()->New<RecvInitialMetadata>();
  }
}

ClientCallData::~ClientCallData() {
  GPR_ASSERT(poll_ctx_ == nullptr);
  GRPC_ERROR_UNREF(cancelled_error_);
  if (recv_initial_metadata_ != nullptr) {
    recv_initial_metadata_->~RecvInitialMetadata();
  }
}

// Activity implementation.
void ClientCallData::ForceImmediateRepoll() {
  GPR_ASSERT(poll_ctx_ != nullptr);
  poll_ctx_->Repoll();
}

// Handle one grpc_transport_stream_op_batch
void ClientCallData::StartBatch(grpc_transport_stream_op_batch* batch) {
  // Fake out the activity based context.
  ScopedContext context(this);

  // If this is a cancel stream, cancel anything we have pending and propagate
  // the cancellation.
  if (batch->cancel_stream) {
    GPR_ASSERT(!batch->send_initial_metadata &&
               !batch->send_trailing_metadata && !batch->send_message &&
               !batch->recv_initial_metadata && !batch->recv_message &&
               !batch->recv_trailing_metadata);
    Cancel(batch->payload->cancel_stream.cancel_error);
    grpc_call_next_op(elem(), batch);
    return;
  }

  if (recv_initial_metadata_ != nullptr && batch->recv_initial_metadata) {
    bool hook = true;
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kInitial:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kHookedWaitingForLatch;
        break;
      case RecvInitialMetadata::kGotLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kHookedAndGotLatch;
        break;
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
        hook = false;
        break;
      case RecvInitialMetadata::kHookedWaitingForLatch:
      case RecvInitialMetadata::kHookedAndGotLatch:
      case RecvInitialMetadata::kCompleteWaitingForLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
      case RecvInitialMetadata::kResponded:
        abort();  // unreachable
    }
    if (hook) {
      auto cb = [](void* ptr, grpc_error_handle error) {
        ClientCallData* self = static_cast<ClientCallData*>(ptr);
        self->RecvInitialMetadataReady(error);
      };
      recv_initial_metadata_->metadata =
          batch->payload->recv_initial_metadata.recv_initial_metadata;
      recv_initial_metadata_->original_on_ready =
          batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
      GRPC_CLOSURE_INIT(&recv_initial_metadata_->on_ready, cb, this, nullptr);
      batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
          &recv_initial_metadata_->on_ready;
    }
  }

  // send_initial_metadata: seeing this triggers the start of the promise part
  // of this filter.
  if (batch->send_initial_metadata) {
    // If we're already cancelled, just terminate the batch.
    if (send_initial_state_ == SendInitialState::kCancelled ||
        recv_trailing_state_ == RecvTrailingState::kCancelled) {
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(cancelled_error_), call_combiner());
      return;
    }
    // Otherwise, we should not have seen a send_initial_metadata op yet.
    GPR_ASSERT(send_initial_state_ == SendInitialState::kInitial);
    // Mark ourselves as queued.
    send_initial_state_ = SendInitialState::kQueued;
    if (batch->recv_trailing_metadata) {
      // If there's a recv_trailing_metadata op, we queue that too.
      GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
      recv_trailing_state_ = RecvTrailingState::kQueued;
    }
    // This is the queuing!
    send_initial_metadata_batch_ = batch;
    // And kick start the promise.
    StartPromise();
    return;
  }

  // recv_trailing_metadata *without* send_initial_metadata: hook it so we can
  // respond to it, and push it down.
  if (batch->recv_trailing_metadata) {
    if (recv_trailing_state_ == RecvTrailingState::kCancelled) {
      grpc_transport_stream_op_batch_finish_with_failure(
          batch, GRPC_ERROR_REF(cancelled_error_), call_combiner());
      return;
    }
    GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial);
    recv_trailing_state_ = RecvTrailingState::kForwarded;
    HookRecvTrailingMetadata(batch);
  }

  grpc_call_next_op(elem(), batch);
}

// Handle cancellation.
void ClientCallData::Cancel(grpc_error_handle error) {
  // Track the latest reason for cancellation.
  GRPC_ERROR_UNREF(cancelled_error_);
  cancelled_error_ = GRPC_ERROR_REF(error);
  // Stop running the promise.
  promise_ = ArenaPromise<ServerMetadataHandle>();
  // If we have an op queued, fail that op.
  // Record what we've done.
  if (send_initial_state_ == SendInitialState::kQueued) {
    send_initial_state_ = SendInitialState::kCancelled;
    if (recv_trailing_state_ == RecvTrailingState::kQueued) {
      recv_trailing_state_ = RecvTrailingState::kCancelled;
    }
    struct FailBatch : public grpc_closure {
      grpc_transport_stream_op_batch* batch;
      CallCombiner* call_combiner;
    };
    auto fail = [](void* p, grpc_error_handle error) {
      auto* f = static_cast<FailBatch*>(p);
      grpc_transport_stream_op_batch_finish_with_failure(
          f->batch, GRPC_ERROR_REF(error), f->call_combiner);
      delete f;
    };
    auto* b = new FailBatch();
    GRPC_CLOSURE_INIT(b, fail, b, nullptr);
    b->batch = absl::exchange(send_initial_metadata_batch_, nullptr);
    b->call_combiner = call_combiner();
    GRPC_CALL_COMBINER_START(call_combiner(), b,
                             GRPC_ERROR_REF(cancelled_error_),
                             "cancel pending batch");
  } else {
    send_initial_state_ = SendInitialState::kCancelled;
  }
  if (recv_initial_metadata_ != nullptr) {
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kCompleteWaitingForLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
        GRPC_CALL_COMBINER_START(
            call_combiner(),
            absl::exchange(recv_initial_metadata_->original_on_ready, nullptr),
            GRPC_ERROR_REF(error), "propagate cancellation");
        break;
      case RecvInitialMetadata::kInitial:
      case RecvInitialMetadata::kGotLatch:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      case RecvInitialMetadata::kHookedWaitingForLatch:
      case RecvInitialMetadata::kHookedAndGotLatch:
      case RecvInitialMetadata::kResponded:
        break;
    }
  }
}

// Begin running the promise - which will ultimately take some initial
// metadata and return some trailing metadata.
void ClientCallData::StartPromise() {
  GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
  ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);

  // Construct the promise.
  PollContext ctx(this);
  promise_ = filter->MakeCallPromise(
      CallArgs{WrapMetadata(send_initial_metadata_batch_->payload
                                ->send_initial_metadata.send_initial_metadata),
               server_initial_metadata_latch()},
      [this](CallArgs call_args) {
        return MakeNextPromise(std::move(call_args));
      });
  ctx.Run();
}

void ClientCallData::RecvInitialMetadataReady(grpc_error_handle error) {
  ScopedContext context(this);
  switch (recv_initial_metadata_->state) {
    case RecvInitialMetadata::kHookedWaitingForLatch:
      recv_initial_metadata_->state =
          RecvInitialMetadata::kCompleteWaitingForLatch;
      break;
    case RecvInitialMetadata::kHookedAndGotLatch:
      recv_initial_metadata_->state = RecvInitialMetadata::kCompleteAndGotLatch;
      break;
    case RecvInitialMetadata::kInitial:
    case RecvInitialMetadata::kGotLatch:
    case RecvInitialMetadata::kCompleteWaitingForLatch:
    case RecvInitialMetadata::kCompleteAndGotLatch:
    case RecvInitialMetadata::kCompleteAndSetLatch:
    case RecvInitialMetadata::kResponded:
    case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
      abort();  // unreachable
  }
  if (error != GRPC_ERROR_NONE) {
    recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
    GRPC_CALL_COMBINER_START(
        call_combiner(),
        absl::exchange(recv_initial_metadata_->original_on_ready, nullptr),
        GRPC_ERROR_REF(error), "propagate cancellation");
  } else if (send_initial_state_ == SendInitialState::kCancelled ||
             recv_trailing_state_ == RecvTrailingState::kResponded) {
    recv_initial_metadata_->state = RecvInitialMetadata::kResponded;
    GRPC_CALL_COMBINER_START(
        call_combiner(),
        absl::exchange(recv_initial_metadata_->original_on_ready, nullptr),
        GRPC_ERROR_REF(cancelled_error_), "propagate cancellation");
  }
  WakeInsideCombiner();
}

// Interject our callback into the op batch for recv trailing metadata ready.
// Stash a pointer to the trailing metadata that will be filled in, so we can
// manipulate it later.
void ClientCallData::HookRecvTrailingMetadata(
    grpc_transport_stream_op_batch* batch) {
  recv_trailing_metadata_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata;
  original_recv_trailing_metadata_ready_ =
      batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready;
  batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready =
      &recv_trailing_metadata_ready_;
}

// Construct a promise that will "call" the next filter.
// Effectively:
//   - put the modified initial metadata into the batch to be sent down.
//   - return a wrapper around PollTrailingMetadata as the promise.
ArenaPromise<ServerMetadataHandle> ClientCallData::MakeNextPromise(
    CallArgs call_args) {
  GPR_ASSERT(poll_ctx_ != nullptr);
  GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
  send_initial_metadata_batch_->payload->send_initial_metadata
      .send_initial_metadata =
      UnwrapMetadata(std::move(call_args.client_initial_metadata));
  if (recv_initial_metadata_ != nullptr) {
    // Call args should contain a latch for receiving initial metadata.
    // It might be the one we passed in - in which case we know this filter
    // only wants to examine the metadata, or it might be a new instance, in
    // which case we know the filter wants to mutate.
    GPR_ASSERT(call_args.server_initial_metadata != nullptr);
    recv_initial_metadata_->server_initial_metadata_publisher =
        call_args.server_initial_metadata;
    switch (recv_initial_metadata_->state) {
      case RecvInitialMetadata::kInitial:
        recv_initial_metadata_->state = RecvInitialMetadata::kGotLatch;
        break;
      case RecvInitialMetadata::kHookedWaitingForLatch:
        recv_initial_metadata_->state = RecvInitialMetadata::kHookedAndGotLatch;
        poll_ctx_->Repoll();
        break;
      case RecvInitialMetadata::kCompleteWaitingForLatch:
        recv_initial_metadata_->state =
            RecvInitialMetadata::kCompleteAndGotLatch;
        poll_ctx_->Repoll();
        break;
      case RecvInitialMetadata::kGotLatch:
      case RecvInitialMetadata::kHookedAndGotLatch:
      case RecvInitialMetadata::kCompleteAndGotLatch:
      case RecvInitialMetadata::kCompleteAndSetLatch:
      case RecvInitialMetadata::kResponded:
      case RecvInitialMetadata::kRespondedToTrailingMetadataPriorToHook:
        abort();  // unreachable
    }
  } else {
    GPR_ASSERT(call_args.server_initial_metadata == nullptr);
  }
  return ArenaPromise<ServerMetadataHandle>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// First poll: send the send_initial_metadata op down the stack.
// All polls: await receiving the trailing metadata, then return it to the
// application.
Poll<ServerMetadataHandle> ClientCallData::PollTrailingMetadata() {
  GPR_ASSERT(poll_ctx_ != nullptr);
  if (send_initial_state_ == SendInitialState::kQueued) {
    // First poll: pass the send_initial_metadata op down the stack.
    GPR_ASSERT(send_initial_metadata_batch_ != nullptr);
    send_initial_state_ = SendInitialState::kForwarded;
    if (recv_trailing_state_ == RecvTrailingState::kQueued) {
      // (and the recv_trailing_metadata op if it's part of the queuing)
      HookRecvTrailingMetadata(send_initial_metadata_batch_);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
    }
    poll_ctx_->ForwardSendInitialMetadata();
  }
  switch (recv_trailing_state_) {
    case RecvTrailingState::kInitial:
    case RecvTrailingState::kQueued:
    case RecvTrailingState::kForwarded:
      // No trailing metadata yet: we are pending.
      // We return that and expect the promise to be repolled later (if it's
      // not cancelled).
      return Pending{};
    case RecvTrailingState::kComplete:
      // We've received trailing metadata: pass it to the promise and allow it
      // to adjust it.
      return WrapMetadata(recv_trailing_metadata_);
    case RecvTrailingState::kCancelled: {
      // We've been cancelled: synthesize some trailing metadata and pass it
      // to the calling promise for adjustment.
      recv_trailing_metadata_->Clear();
      SetStatusFromError(recv_trailing_metadata_, cancelled_error_);
      return WrapMetadata(recv_trailing_metadata_);
    }
    case RecvTrailingState::kResponded:
      // We've already responded to the caller: we can't do anything and we
      // should never reach here.
      abort();
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void ClientCallData::RecvTrailingMetadataReadyCallback(
    void* arg, grpc_error_handle error) {
  static_cast<ClientCallData*>(arg)->RecvTrailingMetadataReady(error);
}

void ClientCallData::RecvTrailingMetadataReady(grpc_error_handle error) {
  // If we were cancelled prior to receiving this callback, we should simply
  // forward the callback up with the same error.
  if (recv_trailing_state_ == RecvTrailingState::kCancelled) {
    if (grpc_closure* call_closure =
            absl::exchange(original_recv_trailing_metadata_ready_, nullptr)) {
      Closure::Run(DEBUG_LOCATION, call_closure, GRPC_ERROR_REF(error));
    }
    return;
  }
  // If there was an error, we'll put that into the trailing metadata and
  // proceed as if there was not.
  if (error != GRPC_ERROR_NONE) {
    SetStatusFromError(recv_trailing_metadata_, error);
  }
  // Record that we've got the callback.
  GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kForwarded);
  recv_trailing_state_ = RecvTrailingState::kComplete;
  // Repoll the promise.
  ScopedContext context(this);
  WakeInsideCombiner();
}

// Given an error, fill in ServerMetadataHandle to represent that error.
void ClientCallData::SetStatusFromError(grpc_metadata_batch* metadata,
                                        grpc_error_handle error) {
  grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
  std::string status_details;
  grpc_error_get_status(error, deadline(), &status_code, &status_details,
                        nullptr, nullptr);
  metadata->Set(GrpcStatusMetadata(), status_code);
  metadata->Set(GrpcMessageMetadata(), Slice::FromCopiedString(status_details));
  metadata->GetOrCreatePointer(GrpcStatusContext())
      ->emplace_back(grpc_error_std_string(error));
}

// Wakeup and poll the promise if appropriate.
void ClientCallData::WakeInsideCombiner() { PollContext(this).Run(); }

void ClientCallData::OnWakeup() {
  ScopedContext context(this);
  WakeInsideCombiner();
}

///////////////////////////////////////////////////////////////////////////////
// ServerCallData

ServerCallData::ServerCallData(grpc_call_element* elem,
                               const grpc_call_element_args* args,
                               uint8_t flags)
    : BaseCallData(elem, args, flags) {
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_,
                    RecvInitialMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
}

ServerCallData::~ServerCallData() {
  GPR_ASSERT(!is_polling_);
  GRPC_ERROR_UNREF(cancelled_error_);
}

// Activity implementation.
void ServerCallData::ForceImmediateRepoll() { abort(); }  // Not implemented.

// Handle one grpc_transport_stream_op_batch
void ServerCallData::StartBatch(grpc_transport_stream_op_batch* batch) {
  // Fake out the activity based context.
  ScopedContext context(this);

  // If this is a cancel stream, cancel anything we have pending and
  // propagate the cancellation.
  if (batch->cancel_stream) {
    GPR_ASSERT(!batch->send_initial_metadata &&
               !batch->send_trailing_metadata && !batch->send_message &&
               !batch->recv_initial_metadata && !batch->recv_message &&
               !batch->recv_trailing_metadata);
    Cancel(batch->payload->cancel_stream.cancel_error);
    grpc_call_next_op(elem(), batch);
    return;
  }

  // recv_initial_metadata: we hook the response of this so we can start the
  // promise at an appropriate time.
  if (batch->recv_initial_metadata) {
    GPR_ASSERT(!batch->send_initial_metadata &&
               !batch->send_trailing_metadata && !batch->send_message &&
               !batch->recv_message && !batch->recv_trailing_metadata);
    // Otherwise, we should not have seen a send_initial_metadata op yet.
    GPR_ASSERT(recv_initial_state_ == RecvInitialState::kInitial);
    // Hook the callback so we know when to start the promise.
    recv_initial_metadata_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata;
    original_recv_initial_metadata_ready_ =
        batch->payload->recv_initial_metadata.recv_initial_metadata_ready;
    batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &recv_initial_metadata_ready_;
    recv_initial_state_ = RecvInitialState::kForwarded;
  }

  // send_trailing_metadata
  if (batch->send_trailing_metadata) {
    switch (send_trailing_state_) {
      case SendTrailingState::kInitial:
        send_trailing_metadata_batch_ = batch;
        send_trailing_state_ = SendTrailingState::kQueued;
        WakeInsideCombiner([this](grpc_error_handle error) {
          GPR_ASSERT(send_trailing_state_ == SendTrailingState::kQueued);
          Cancel(error);
        });
        break;
      case SendTrailingState::kQueued:
      case SendTrailingState::kForwarded:
        abort();  // unreachable
        break;
      case SendTrailingState::kCancelled:
        grpc_transport_stream_op_batch_finish_with_failure(
            batch, GRPC_ERROR_REF(cancelled_error_), call_combiner());
        break;
    }
    return;
  }

  grpc_call_next_op(elem(), batch);
}

// Handle cancellation.
void ServerCallData::Cancel(grpc_error_handle error) {
  // Track the latest reason for cancellation.
  GRPC_ERROR_UNREF(cancelled_error_);
  cancelled_error_ = GRPC_ERROR_REF(error);
  // Stop running the promise.
  promise_ = ArenaPromise<ServerMetadataHandle>();
  if (send_trailing_state_ == SendTrailingState::kQueued) {
    send_trailing_state_ = SendTrailingState::kCancelled;
    struct FailBatch : public grpc_closure {
      grpc_transport_stream_op_batch* batch;
      CallCombiner* call_combiner;
    };
    auto fail = [](void* p, grpc_error_handle error) {
      auto* f = static_cast<FailBatch*>(p);
      grpc_transport_stream_op_batch_finish_with_failure(
          f->batch, GRPC_ERROR_REF(error), f->call_combiner);
      delete f;
    };
    auto* b = new FailBatch();
    GRPC_CLOSURE_INIT(b, fail, b, nullptr);
    b->batch = absl::exchange(send_trailing_metadata_batch_, nullptr);
    b->call_combiner = call_combiner();
    GRPC_CALL_COMBINER_START(call_combiner(), b,
                             GRPC_ERROR_REF(cancelled_error_),
                             "cancel pending batch");
  } else {
    send_trailing_state_ = SendTrailingState::kCancelled;
  }
}

// Construct a promise that will "call" the next filter.
// Effectively:
//   - put the modified initial metadata into the batch being sent up.
//   - return a wrapper around PollTrailingMetadata as the promise.
ArenaPromise<ServerMetadataHandle> ServerCallData::MakeNextPromise(
    CallArgs call_args) {
  GPR_ASSERT(recv_initial_state_ == RecvInitialState::kComplete);
  GPR_ASSERT(UnwrapMetadata(std::move(call_args.client_initial_metadata)) ==
             recv_initial_metadata_);
  forward_recv_initial_metadata_callback_ = true;
  return ArenaPromise<ServerMetadataHandle>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// All polls: await sending the trailing metadata, then foward it down the
// stack.
Poll<ServerMetadataHandle> ServerCallData::PollTrailingMetadata() {
  switch (send_trailing_state_) {
    case SendTrailingState::kInitial:
      return Pending{};
    case SendTrailingState::kQueued:
      return WrapMetadata(send_trailing_metadata_batch_->payload
                              ->send_trailing_metadata.send_trailing_metadata);
    case SendTrailingState::kForwarded:
      abort();  // unreachable
    case SendTrailingState::kCancelled:
      // We could translate cancelled_error to metadata and return it... BUT
      // we're not gonna be running much longer and the results going to be
      // ignored.
      return Pending{};
  }
  GPR_UNREACHABLE_CODE(return Pending{});
}

void ServerCallData::RecvInitialMetadataReadyCallback(void* arg,
                                                      grpc_error_handle error) {
  static_cast<ServerCallData*>(arg)->RecvInitialMetadataReady(error);
}

void ServerCallData::RecvInitialMetadataReady(grpc_error_handle error) {
  GPR_ASSERT(recv_initial_state_ == RecvInitialState::kForwarded);
  // If there was an error we just propagate that through
  if (error != GRPC_ERROR_NONE) {
    recv_initial_state_ = RecvInitialState::kResponded;
    Closure::Run(DEBUG_LOCATION, original_recv_initial_metadata_ready_,
                 GRPC_ERROR_REF(error));
    return;
  }
  // Record that we've got the callback.
  recv_initial_state_ = RecvInitialState::kComplete;

  // Start the promise.
  ScopedContext context(this);
  // Construct the promise.
  ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);
  promise_ =
      filter->MakeCallPromise(CallArgs{WrapMetadata(recv_initial_metadata_),
                                       server_initial_metadata_latch()},
                              [this](CallArgs call_args) {
                                return MakeNextPromise(std::move(call_args));
                              });
  // Poll once.
  bool own_error = false;
  WakeInsideCombiner([&error, &own_error](grpc_error_handle new_error) {
    GPR_ASSERT(error == GRPC_ERROR_NONE);
    error = GRPC_ERROR_REF(new_error);
    own_error = true;
  });
  Closure::Run(DEBUG_LOCATION, original_recv_initial_metadata_ready_,
               GRPC_ERROR_REF(error));
  if (own_error) GRPC_ERROR_UNREF(error);
}

// Wakeup and poll the promise if appropriate.
void ServerCallData::WakeInsideCombiner(
    absl::FunctionRef<void(grpc_error_handle)> cancel) {
  GPR_ASSERT(!is_polling_);
  bool forward_send_trailing_metadata = false;
  is_polling_ = true;
  if (recv_initial_state_ == RecvInitialState::kComplete) {
    Poll<ServerMetadataHandle> poll;
    {
      ScopedActivity activity(this);
      poll = promise_();
    }
    if (auto* r = absl::get_if<ServerMetadataHandle>(&poll)) {
      auto* md = UnwrapMetadata(std::move(*r));
      bool destroy_md = true;
      switch (send_trailing_state_) {
        case SendTrailingState::kQueued: {
          if (send_trailing_metadata_batch_->payload->send_trailing_metadata
                  .send_trailing_metadata != md) {
            *send_trailing_metadata_batch_->payload->send_trailing_metadata
                 .send_trailing_metadata = std::move(*md);
          } else {
            destroy_md = false;
          }
          forward_send_trailing_metadata = true;
          send_trailing_state_ = SendTrailingState::kForwarded;
        } break;
        case SendTrailingState::kForwarded:
          abort();  // unreachable
          break;
        case SendTrailingState::kInitial: {
          GPR_ASSERT(*md->get_pointer(GrpcStatusMetadata()) != GRPC_STATUS_OK);
          grpc_error_handle error =
              grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                     "early return from promise based filter"),
                                 GRPC_ERROR_INT_GRPC_STATUS,
                                 *md->get_pointer(GrpcStatusMetadata()));
          if (auto* message = md->get_pointer(GrpcMessageMetadata())) {
            error = grpc_error_set_str(error, GRPC_ERROR_STR_GRPC_MESSAGE,
                                       message->as_string_view());
          }
          cancel(error);
          GRPC_ERROR_UNREF(error);
        } break;
        case SendTrailingState::kCancelled:
          // Nothing to do.
          break;
      }
      if (destroy_md) {
        md->~grpc_metadata_batch();
      }
    }
  }
  is_polling_ = false;
  if (forward_send_trailing_metadata) {
    grpc_call_next_op(elem(),
                      absl::exchange(send_trailing_metadata_batch_, nullptr));
  }
}

void ServerCallData::OnWakeup() { abort(); }  // not implemented

}  // namespace promise_filter_detail
}  // namespace grpc_core
