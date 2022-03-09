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

#include "src/core/lib/channel/channel_stack.h"

namespace grpc_core {
namespace promise_filter_detail {

///////////////////////////////////////////////////////////////////////////////
// BaseCallData

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

ClientCallData::ClientCallData(grpc_call_element* elem,
                               const grpc_call_element_args* args)
    : BaseCallData(elem, args) {
  GRPC_CLOSURE_INIT(&recv_trailing_metadata_ready_,
                    RecvTrailingMetadataReadyCallback, this,
                    grpc_schedule_on_exec_ctx);
}

ClientCallData::~ClientCallData() {
  GPR_ASSERT(!is_polling_);
  GRPC_ERROR_UNREF(cancelled_error_);
}

// Activity implementation.
void ClientCallData::ForceImmediateRepoll() {
  GPR_ASSERT(is_polling_);
  repoll_ = true;
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
  promise_ = ArenaPromise<TrailingMetadata>();
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
}

// Begin running the promise - which will ultimately take some initial
// metadata and return some trailing metadata.
void ClientCallData::StartPromise() {
  GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
  ChannelFilter* filter = static_cast<ChannelFilter*>(elem()->channel_data);

  // Construct the promise.
  {
    ScopedActivity activity(this);
    promise_ = filter->MakeCallPromise(
        WrapMetadata(send_initial_metadata_batch_->payload
                         ->send_initial_metadata.send_initial_metadata),
        [this](ClientInitialMetadata initial_metadata) {
          return MakeNextPromise(std::move(initial_metadata));
        });
  }
  // Poll once.
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
ArenaPromise<TrailingMetadata> ClientCallData::MakeNextPromise(
    ClientInitialMetadata initial_metadata) {
  GPR_ASSERT(send_initial_state_ == SendInitialState::kQueued);
  send_initial_metadata_batch_->payload->send_initial_metadata
      .send_initial_metadata = UnwrapMetadata(std::move(initial_metadata));
  return ArenaPromise<TrailingMetadata>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// First poll: send the send_initial_metadata op down the stack.
// All polls: await receiving the trailing metadata, then return it to the
// application.
Poll<TrailingMetadata> ClientCallData::PollTrailingMetadata() {
  if (send_initial_state_ == SendInitialState::kQueued) {
    // First poll: pass the send_initial_metadata op down the stack.
    GPR_ASSERT(send_initial_metadata_batch_ != nullptr);
    send_initial_state_ = SendInitialState::kForwarded;
    if (recv_trailing_state_ == RecvTrailingState::kQueued) {
      // (and the recv_trailing_metadata op if it's part of the queuing)
      HookRecvTrailingMetadata(send_initial_metadata_batch_);
      recv_trailing_state_ = RecvTrailingState::kForwarded;
    }
    forward_send_initial_metadata_ = true;
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

// Given an error, fill in TrailingMetadata to represent that error.
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
void ClientCallData::WakeInsideCombiner() {
  GPR_ASSERT(!is_polling_);
  grpc_closure* call_closure = nullptr;
  is_polling_ = true;
  grpc_error_handle cancel_send_initial_metadata_error = GRPC_ERROR_NONE;
  grpc_transport_stream_op_batch* forward_batch = nullptr;
  switch (send_initial_state_) {
    case SendInitialState::kQueued:
    case SendInitialState::kForwarded: {
      // Poll the promise once since we're waiting for it.
      Poll<TrailingMetadata> poll;
      {
        ScopedActivity activity(this);
        poll = promise_();
      }
      if (auto* r = absl::get_if<TrailingMetadata>(&poll)) {
        promise_ = ArenaPromise<TrailingMetadata>();
        auto* md = UnwrapMetadata(std::move(*r));
        bool destroy_md = true;
        if (recv_trailing_state_ == RecvTrailingState::kComplete) {
          if (recv_trailing_metadata_ != md) {
            *recv_trailing_metadata_ = std::move(*md);
          } else {
            destroy_md = false;
          }
          recv_trailing_state_ = RecvTrailingState::kResponded;
          call_closure =
              absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
        } else {
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
          GRPC_ERROR_UNREF(cancelled_error_);
          cancelled_error_ = GRPC_ERROR_REF(error);
          if (send_initial_state_ == SendInitialState::kQueued) {
            send_initial_state_ = SendInitialState::kCancelled;
            cancel_send_initial_metadata_error = error;
          } else {
            GPR_ASSERT(recv_trailing_state_ == RecvTrailingState::kInitial ||
                       recv_trailing_state_ == RecvTrailingState::kForwarded);
            call_combiner()->Cancel(GRPC_ERROR_REF(error));
            forward_batch = grpc_make_transport_stream_op(GRPC_CLOSURE_CREATE(
                [](void*, grpc_error_handle) {}, nullptr, nullptr));
            forward_batch->cancel_stream = true;
            forward_batch->payload->cancel_stream.cancel_error = error;
          }
          recv_trailing_state_ = RecvTrailingState::kCancelled;
        }
        if (destroy_md) {
          md->~grpc_metadata_batch();
        }
      }
    } break;
    case SendInitialState::kInitial:
    case SendInitialState::kCancelled:
      // If we get a response without sending anything, we just propagate
      // that up. (note: that situation isn't possible once we finish the
      // promise transition).
      if (recv_trailing_state_ == RecvTrailingState::kComplete) {
        recv_trailing_state_ = RecvTrailingState::kResponded;
        call_closure =
            absl::exchange(original_recv_trailing_metadata_ready_, nullptr);
      }
      break;
  }
  GRPC_CALL_STACK_REF(call_stack(), "finish_poll");
  is_polling_ = false;
  bool in_combiner = true;
  bool repoll = absl::exchange(repoll_, false);
  if (forward_batch != nullptr) {
    GPR_ASSERT(in_combiner);
    in_combiner = false;
    forward_send_initial_metadata_ = false;
    grpc_call_next_op(elem(), forward_batch);
  }
  if (cancel_send_initial_metadata_error != GRPC_ERROR_NONE) {
    GPR_ASSERT(in_combiner);
    forward_send_initial_metadata_ = false;
    in_combiner = false;
    grpc_transport_stream_op_batch_finish_with_failure(
        absl::exchange(send_initial_metadata_batch_, nullptr),
        cancel_send_initial_metadata_error, call_combiner());
  }
  if (absl::exchange(forward_send_initial_metadata_, false)) {
    GPR_ASSERT(in_combiner);
    in_combiner = false;
    grpc_call_next_op(elem(),
                      absl::exchange(send_initial_metadata_batch_, nullptr));
  }
  if (call_closure != nullptr) {
    GPR_ASSERT(in_combiner);
    in_combiner = false;
    Closure::Run(DEBUG_LOCATION, call_closure, GRPC_ERROR_NONE);
  }
  if (repoll) {
    if (in_combiner) {
      WakeInsideCombiner();
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
      auto* p = new NextPoll;
      GRPC_CALL_STACK_REF(call_stack(), "re-poll");
      GRPC_CLOSURE_INIT(p, run, p, nullptr);
      GRPC_CALL_COMBINER_START(call_combiner(), p, GRPC_ERROR_NONE, "re-poll");
    }
  } else if (in_combiner) {
    GRPC_CALL_COMBINER_STOP(call_combiner(), "poll paused");
  }
  GRPC_CALL_STACK_UNREF(call_stack(), "finish_poll");
}

void ClientCallData::OnWakeup() {
  ScopedContext context(this);
  WakeInsideCombiner();
}

///////////////////////////////////////////////////////////////////////////////
// ServerCallData

ServerCallData::ServerCallData(grpc_call_element* elem,
                               const grpc_call_element_args* args)
    : BaseCallData(elem, args) {
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
  promise_ = ArenaPromise<TrailingMetadata>();
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
ArenaPromise<TrailingMetadata> ServerCallData::MakeNextPromise(
    ClientInitialMetadata initial_metadata) {
  GPR_ASSERT(recv_initial_state_ == RecvInitialState::kComplete);
  GPR_ASSERT(UnwrapMetadata(std::move(initial_metadata)) ==
             recv_initial_metadata_);
  forward_recv_initial_metadata_callback_ = true;
  return ArenaPromise<TrailingMetadata>(
      [this]() { return PollTrailingMetadata(); });
}

// Wrapper to make it look like we're calling the next filter as a promise.
// All polls: await sending the trailing metadata, then foward it down the
// stack.
Poll<TrailingMetadata> ServerCallData::PollTrailingMetadata() {
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
  promise_ = filter->MakeCallPromise(
      WrapMetadata(recv_initial_metadata_),
      [this](ClientInitialMetadata initial_metadata) {
        return MakeNextPromise(std::move(initial_metadata));
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
    Poll<TrailingMetadata> poll;
    {
      ScopedActivity activity(this);
      poll = promise_();
    }
    if (auto* r = absl::get_if<TrailingMetadata>(&poll)) {
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
