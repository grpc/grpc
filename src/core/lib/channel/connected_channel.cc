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

#include "src/core/lib/channel/connected_channel.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "absl/utility/utility.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_allocator.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/core/lib/transport/transport_impl.h"

#define MAX_BUFFER_LENGTH 8192

typedef struct connected_channel_channel_data {
  grpc_transport* transport;
} channel_data;

struct callback_state {
  grpc_closure closure;
  grpc_closure* original_closure;
  grpc_core::CallCombiner* call_combiner;
  const char* reason;
};
typedef struct connected_channel_call_data {
  grpc_core::CallCombiner* call_combiner;
  // Closures used for returning results on the call combiner.
  callback_state on_complete[6];  // Max number of pending batches.
  callback_state recv_initial_metadata_ready;
  callback_state recv_message_ready;
  callback_state recv_trailing_metadata_ready;
} call_data;

static void run_in_call_combiner(void* arg, grpc_error_handle error) {
  callback_state* state = static_cast<callback_state*>(arg);
  GRPC_CALL_COMBINER_START(state->call_combiner, state->original_closure,
                           GRPC_ERROR_REF(error), state->reason);
}

static void run_cancel_in_call_combiner(void* arg, grpc_error_handle error) {
  run_in_call_combiner(arg, error);
  gpr_free(arg);
}

static void intercept_callback(call_data* calld, callback_state* state,
                               bool free_when_done, const char* reason,
                               grpc_closure** original_closure) {
  state->original_closure = *original_closure;
  state->call_combiner = calld->call_combiner;
  state->reason = reason;
  *original_closure = GRPC_CLOSURE_INIT(
      &state->closure,
      free_when_done ? run_cancel_in_call_combiner : run_in_call_combiner,
      state, grpc_schedule_on_exec_ctx);
}

static callback_state* get_state_for_batch(
    call_data* calld, grpc_transport_stream_op_batch* batch) {
  if (batch->send_initial_metadata) return &calld->on_complete[0];
  if (batch->send_message) return &calld->on_complete[1];
  if (batch->send_trailing_metadata) return &calld->on_complete[2];
  if (batch->recv_initial_metadata) return &calld->on_complete[3];
  if (batch->recv_message) return &calld->on_complete[4];
  if (batch->recv_trailing_metadata) return &calld->on_complete[5];
  GPR_UNREACHABLE_CODE(return nullptr);
}

/* We perform a small hack to locate transport data alongside the connected
   channel data in call allocations, to allow everything to be pulled in minimal
   cache line requests */
#define TRANSPORT_STREAM_FROM_CALL_DATA(calld) \
  ((grpc_stream*)(((char*)(calld)) +           \
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(call_data))))
#define CALL_DATA_FROM_TRANSPORT_STREAM(transport_stream) \
  ((call_data*)(((char*)(transport_stream)) -             \
                GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(call_data))))

/* Intercept a call operation and either push it directly up or translate it
   into transport stream operations */
static void connected_channel_start_transport_stream_op_batch(
    grpc_call_element* elem, grpc_transport_stream_op_batch* batch) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  if (batch->recv_initial_metadata) {
    callback_state* state = &calld->recv_initial_metadata_ready;
    intercept_callback(
        calld, state, false, "recv_initial_metadata_ready",
        &batch->payload->recv_initial_metadata.recv_initial_metadata_ready);
  }
  if (batch->recv_message) {
    callback_state* state = &calld->recv_message_ready;
    intercept_callback(calld, state, false, "recv_message_ready",
                       &batch->payload->recv_message.recv_message_ready);
  }
  if (batch->recv_trailing_metadata) {
    callback_state* state = &calld->recv_trailing_metadata_ready;
    intercept_callback(
        calld, state, false, "recv_trailing_metadata_ready",
        &batch->payload->recv_trailing_metadata.recv_trailing_metadata_ready);
  }
  if (batch->cancel_stream) {
    // There can be more than one cancellation batch in flight at any
    // given time, so we can't just pick out a fixed index into
    // calld->on_complete like we can for the other ops.  However,
    // cancellation isn't in the fast path, so we just allocate a new
    // closure for each one.
    callback_state* state =
        static_cast<callback_state*>(gpr_malloc(sizeof(*state)));
    intercept_callback(calld, state, true, "on_complete (cancel_stream)",
                       &batch->on_complete);
  } else if (batch->on_complete != nullptr) {
    callback_state* state = get_state_for_batch(calld, batch);
    intercept_callback(calld, state, false, "on_complete", &batch->on_complete);
  }
  grpc_transport_perform_stream_op(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld), batch);
  GRPC_CALL_COMBINER_STOP(calld->call_combiner, "passed batch to transport");
}

static void connected_channel_start_transport_op(grpc_channel_element* elem,
                                                 grpc_transport_op* op) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_perform_op(chand->transport, op);
}

/* Constructor for call_data */
static grpc_error_handle connected_channel_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  calld->call_combiner = args->call_combiner;
  int r = grpc_transport_init_stream(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld),
      &args->call_stack->refcount, args->server_transport_data, args->arena);
  return r == 0 ? GRPC_ERROR_NONE
                : GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                      "transport stream initialization failed");
}

static void set_pollset_or_pollset_set(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_set_pops(chand->transport,
                          TRANSPORT_STREAM_FROM_CALL_DATA(calld), pollent);
}

/* Destructor for call_data */
static void connected_channel_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_destroy_stream(chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                then_schedule_closure);
}

/* Constructor for channel_data */
static grpc_error_handle connected_channel_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(args->is_last);
  cd->transport = grpc_channel_args_find_pointer<grpc_transport>(
      args->channel_args, GRPC_ARG_TRANSPORT);
  return GRPC_ERROR_NONE;
}

/* Destructor for channel_data */
static void connected_channel_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  if (cd->transport) {
    grpc_transport_destroy(cd->transport);
  }
}

/* No-op. */
static void connected_channel_get_channel_info(
    grpc_channel_element* /*elem*/, const grpc_channel_info* /*channel_info*/) {
}

namespace grpc_core {

class ClientConnectedCallPromise {
 public:
  ClientConnectedCallPromise(grpc_transport* transport, CallArgs call_args)
      : impl_(GetContext<Arena>()->ManagedNew<Impl>(transport,
                                                    std::move(call_args))) {}

  ~ClientConnectedCallPromise() {
    if (impl_ != nullptr) {
      impl_->DropStream();
    }
  }

  ClientConnectedCallPromise(const ClientConnectedCallPromise&) = delete;
  ClientConnectedCallPromise& operator=(const ClientConnectedCallPromise&) =
      delete;
  ClientConnectedCallPromise(ClientConnectedCallPromise&& other) noexcept
      : impl_(absl::exchange(other.impl_, nullptr)) {}
  ClientConnectedCallPromise& operator=(
      ClientConnectedCallPromise&& other) noexcept {
    impl_ = absl::exchange(impl_, other.impl_);
    return *this;
  }

  static ArenaPromise<ServerMetadataHandle> Make(grpc_transport* transport,
                                                 CallArgs call_args) {
    return ClientConnectedCallPromise(transport, std::move(call_args));
  }

  Poll<ServerMetadataHandle> operator()() { return impl_->PollOnce(); }

 private:
  class Impl {
   public:
    Impl(grpc_transport* transport, CallArgs call_args)
        : transport_(transport),
          stream_(nullptr, StreamDeleter(transport)),
          server_initial_metadata_latch_(call_args.server_initial_metadata),
          client_to_server_messages_(call_args.client_to_server_messages),
          server_to_client_messages_(call_args.server_to_client_messages),
          client_initial_metadata_(
              std::move(call_args.client_initial_metadata)) {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO, "%sInitImpl: intitial_metadata=%s",
                Activity::current()->DebugTag().c_str(),
                client_initial_metadata_->DebugString().c_str());
      }
    }

    void DropStream() {
      if (stream_ == nullptr) {
        return;
      } else if (!finished_) {
        auto* cancel_op =
            GetContext<Arena>()->New<grpc_transport_stream_op_batch>();
        cancel_op->cancel_stream = true;
        cancel_op->payload = &batch_payload_;
        auto* stream = stream_.get();
        cancel_op->on_complete =
            NewClosure([stream = std::move(stream_)](grpc_error_handle) {});
        batch_payload_.cancel_stream.cancel_error = GRPC_ERROR_CANCELLED;
        grpc_transport_perform_stream_op(transport_, stream, cancel_op);
      } else {
        stream_.reset();
      }
    }

    Poll<ServerMetadataHandle> PollOnce() {
      GPR_ASSERT(!finished_);

      auto push_recv_message = [this] {
        recv_message_state_ = PendingReceiveMessage{};
        auto& pending_recv_message =
            absl::get<PendingReceiveMessage>(recv_message_state_);
        memset(&recv_message_, 0, sizeof(recv_message_));
        recv_message_.payload = &batch_payload_;
        recv_message_.on_complete = nullptr;
        recv_message_.recv_message = true;
        batch_payload_.recv_message.recv_message =
            &pending_recv_message.payload;
        batch_payload_.recv_message.flags = &pending_recv_message.flags;
        batch_payload_.recv_message.call_failed_before_recv_message = nullptr;
        batch_payload_.recv_message.recv_message_ready =
            &recv_message_batch_done_;
        recv_message_waker_ = Activity::current()->MakeOwningWaker();
        push_recv_message_ = true;
        SchedulePush();
      };

      if (!absl::exchange(requested_metadata_, true)) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%sPollConnectedChannel: requesting metadata",
                  Activity::current()->DebugTag().c_str());
        }
        call_context_->IncrementRefCount("child_stream");
        stream_.reset(static_cast<grpc_stream*>(
            GetContext<Arena>()->Alloc(transport_->vtable->sizeof_stream)));
        grpc_transport_init_stream(transport_, stream_.get(),
                                   call_context_->c_stream_refcount(), nullptr,
                                   GetContext<Arena>());
        memset(&metadata_, 0, sizeof(metadata_));
        metadata_.send_initial_metadata = true;
        metadata_.recv_initial_metadata = true;
        metadata_.recv_trailing_metadata = true;
        metadata_.payload = &batch_payload_;
        metadata_.on_complete = &metadata_batch_done_;
        batch_payload_.send_initial_metadata.send_initial_metadata =
            client_initial_metadata_.get();
        // DO NOT SUBMIT: figure this field out
        batch_payload_.send_initial_metadata.send_initial_metadata_flags = 0;
        // DO NOT SUBMIT: figure this field out
        batch_payload_.send_initial_metadata.peer_string = nullptr;
        server_initial_metadata_ =
            GetContext<MetadataAllocator>()->MakeMetadata<ServerMetadata>();
        batch_payload_.recv_initial_metadata.recv_initial_metadata =
            server_initial_metadata_.get();
        batch_payload_.recv_initial_metadata.recv_initial_metadata_ready =
            &recv_initial_metadata_ready_;
        // DO NOT SUBMIT: figure this field out
        batch_payload_.recv_initial_metadata.recv_flags = nullptr;
        // DO NOT SUBMIT: figure this field out
        batch_payload_.recv_initial_metadata.trailing_metadata_available =
            nullptr;
        // DO NOT SUBMIT: figure this field out
        batch_payload_.recv_initial_metadata.peer_string = nullptr;
        server_trailing_metadata_ =
            GetContext<MetadataAllocator>()->MakeMetadata<ServerMetadata>();
        batch_payload_.recv_trailing_metadata.recv_trailing_metadata =
            server_trailing_metadata_.get();
        batch_payload_.recv_trailing_metadata.collect_stats =
            &GetContext<grpc_call_stats>()->transport_stream_stats;
        batch_payload_.recv_trailing_metadata.recv_trailing_metadata_ready =
            &recv_trailing_metadata_ready_;
        push_metadata_ = true;
        call_context_->IncrementRefCount("metadata_batch_done");
        initial_metadata_waker_ = Activity::current()->MakeOwningWaker();
        trailing_metadata_waker_ = Activity::current()->MakeOwningWaker();
        SchedulePush();
      }
      if (absl::holds_alternative<Idle>(send_message_state_)) {
        send_message_state_ = client_to_server_messages_->Next();
      }
      if (auto* next = absl::get_if<PipeReceiver<Message>::NextType>(
              &send_message_state_)) {
        auto r = (*next)();
        if (auto* p = absl::get_if<absl::optional<Message>>(&r)) {
          memset(&send_message_, 0, sizeof(send_message_));
          send_message_.payload = &batch_payload_;
          send_message_.on_complete = &send_message_batch_done_;
          if (p->has_value()) {
            send_message_state_ = std::move(**p);
            auto& msg = absl::get<Message>(send_message_state_);
            send_message_.send_message = true;
            batch_payload_.send_message.send_message = msg.payload();
            batch_payload_.send_message.flags = msg.flags();
          } else {
            GPR_ASSERT(!absl::holds_alternative<Closed>(send_message_state_));
            client_trailing_metadata_ =
                GetContext<MetadataAllocator>()->MakeMetadata<ClientMetadata>();
            send_message_state_ = Closed{};
            send_message_.send_trailing_metadata = true;
            batch_payload_.send_trailing_metadata.send_trailing_metadata =
                client_trailing_metadata_.get();
            // DO NOT SUBMIT: figure this field out
            batch_payload_.send_trailing_metadata.sent = nullptr;
          }
          send_message_waker_ = Activity::current()->MakeOwningWaker();
          push_send_message_ = true;
          SchedulePush();
        }
      }
      if (absl::holds_alternative<Idle>(recv_message_state_)) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%sPollConnectedChannel: requesting message",
                  Activity::current()->DebugTag().c_str());
        }
        push_recv_message();
      }
      if (auto* message =
              absl::get_if<absl::optional<Message>>(&recv_message_state_)) {
        if (message->has_value()) {
          if (grpc_call_trace.enabled()) {
            gpr_log(GPR_INFO,
                    "%sPollConnectedChannel: received message; pushing up",
                    Activity::current()->DebugTag().c_str());
          }
          recv_message_state_ =
              server_to_client_messages_->Push(std::move(**message));
        } else {
          if (grpc_call_trace.enabled()) {
            gpr_log(GPR_INFO,
                    "%sPollConnectedChannel: received no message, closing pipe",
                    Activity::current()->DebugTag().c_str());
          }
          recv_message_state_ = Closed{};
          absl::exchange(server_to_client_messages_, nullptr)->Close();
        }
      }
      if (auto* push = absl::get_if<PipeSender<Message>::PushType>(
              &recv_message_state_)) {
        auto r = (*push)();
        if (bool* result = absl::get_if<bool>(&r)) {
          if (*result) {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%sPollConnectedChannel: pushed message; requesting next",
                      Activity::current()->DebugTag().c_str());
            }
            push_recv_message();
          } else {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%sPollConnectedChannel: failed to push message; marking "
                      "closed",
                      Activity::current()->DebugTag().c_str());
            }
            recv_message_state_ = Closed{};
          }
        }
      }
      if (absl::exchange(queued_initial_metadata_, false)) {
        server_initial_metadata_latch_->Set(server_initial_metadata_.get());
      }
      if (absl::exchange(queued_trailing_metadata_, false)) {
        finished_ = true;
        stream_.reset();
        return ServerMetadataHandle(std::move(server_trailing_metadata_));
      }
      return Pending{};
    }

    void RecvInitialMetadataReady(grpc_error_handle error) {
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      queued_initial_metadata_ = true;
      initial_metadata_waker_.Wakeup();
    }

    void RecvTrailingMetadataReady(grpc_error_handle error) {
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      queued_trailing_metadata_ = true;
      trailing_metadata_waker_.Wakeup();
    }

    void MetadataBatchDone(grpc_error_handle error) {
      GPR_ASSERT(error == GRPC_ERROR_NONE);
      call_context_->Unref("metadata_batch_done");
    }

    void SendMessageBatchDone(grpc_error_handle error) {
      if (error != GRPC_ERROR_NONE) {
        send_message_state_ = Closed{};
      }
      if (!absl::holds_alternative<Closed>(send_message_state_)) {
        send_message_state_ = Idle{};
      }
      send_message_waker_.Wakeup();
    }

    void RecvMessageBatchDone(grpc_error_handle error) {
      if (error != GRPC_ERROR_NONE) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%sRecvMessageBatchDone: error=%s",
                  recv_message_waker_.ActivityDebugTag().c_str(),
                  grpc_error_std_string(error).c_str());
        }
        return;
      }
      if (absl::holds_alternative<Closed>(recv_message_state_)) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%sRecvMessageBatchDone: already closed, ignoring",
                  recv_message_waker_.ActivityDebugTag().c_str());
        }
        return;
      }
      auto pending = MatchMutable(
          &recv_message_state_, [](Idle*) -> PendingReceiveMessage { abort(); },
          [](PendingReceiveMessage* p) -> PendingReceiveMessage {
            return std::move(*p);
          },
          [](absl::optional<Message>*) -> PendingReceiveMessage { abort(); },
          [](Closed*) -> PendingReceiveMessage { abort(); },
          [](PipeSender<Message>::PushType*) -> PendingReceiveMessage {
            abort();
          });
      if (pending.payload.has_value()) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "%sRecvMessageBatchDone: received payload of %" PRIdPTR
                  " bytes",
                  recv_message_waker_.ActivityDebugTag().c_str(),
                  pending.payload->Length());
        }
        recv_message_state_ = absl::optional<Message>(
            Message(std::move(*pending.payload), pending.flags));
      } else {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%sRecvMessageBatchDone: received no payload",
                  recv_message_waker_.ActivityDebugTag().c_str());
        }
        recv_message_state_ = absl::optional<Message>();
      }
      recv_message_waker_.Wakeup();
    }

    void Push() {
      auto do_push = [this](grpc_transport_stream_op_batch* batch) {
        if (stream_ != nullptr) {
          grpc_transport_perform_stream_op(transport_, stream_.get(), batch);
        } else {
          grpc_transport_stream_op_batch_finish_with_failure_without_call_combiner(
              batch, GRPC_ERROR_CANCELLED);
        }
      };
      if (absl::exchange(push_metadata_, false)) {
        do_push(&metadata_);
      }
      if (absl::exchange(push_send_message_, false)) {
        do_push(&send_message_);
      }
      if (absl::exchange(push_recv_message_, false)) {
        do_push(&recv_message_);
      }
      scheduled_push_ = false;
      call_context_->Unref("push");
    }

    void StreamDestroyed() { call_context_->Unref("child_stream"); }

   private:
    struct Idle {};
    struct Closed {};

    class StreamDeleter {
     public:
      explicit StreamDeleter(grpc_transport* transport)
          : transport_(transport) {}
      void operator()(grpc_stream* stream) const {
        if (stream == nullptr) return;
        grpc_transport_destroy_stream(transport_, stream, nullptr);
      }

     private:
      grpc_transport* transport_;
    };
    using StreamPtr = std::unique_ptr<grpc_stream, StreamDeleter>;

    void SchedulePush() {
      if (absl::exchange(scheduled_push_, true)) return;
      call_context_->IncrementRefCount("push");
      ExecCtx::Run(DEBUG_LOCATION, &push_, GRPC_ERROR_NONE);
    }

    bool requested_metadata_ = false;
    bool push_metadata_ = false;
    bool push_send_message_ = false;
    bool push_recv_message_ = false;
    bool scheduled_push_ = false;
    bool queued_initial_metadata_ = false;
    bool queued_trailing_metadata_ = false;
    bool finished_ = false;
    CallContext* const call_context_{GetContext<CallContext>()};
    Waker initial_metadata_waker_;
    Waker trailing_metadata_waker_;
    Waker send_message_waker_;
    Waker recv_message_waker_;
    grpc_transport* const transport_;
    StreamPtr stream_;
    Latch<ServerMetadata*>* server_initial_metadata_latch_;
    PipeReceiver<Message>* client_to_server_messages_;
    PipeSender<Message>* server_to_client_messages_;
    absl::variant<Idle, Closed, PipeReceiver<Message>::NextType, Message>
        send_message_state_;
    struct PendingReceiveMessage {
      absl::optional<SliceBuffer> payload;
      uint32_t flags;
    };
    absl::variant<Idle, PendingReceiveMessage, absl::optional<Message>, Closed,
                  PipeSender<Message>::PushType>
        recv_message_state_;
    grpc_closure recv_initial_metadata_ready_ =
        MakeMemberClosure<Impl, &Impl::RecvInitialMetadataReady>(this);
    grpc_closure recv_trailing_metadata_ready_ =
        MakeMemberClosure<Impl, &Impl::RecvTrailingMetadataReady>(this);
    grpc_closure push_ = MakeMemberClosure<Impl, &Impl::Push>(this);
    ClientMetadataHandle client_initial_metadata_;
    ClientMetadataHandle client_trailing_metadata_;
    ServerMetadataHandle server_initial_metadata_;
    ServerMetadataHandle server_trailing_metadata_;
    grpc_transport_stream_op_batch metadata_;
    grpc_closure metadata_batch_done_ =
        MakeMemberClosure<Impl, &Impl::MetadataBatchDone>(this);
    grpc_transport_stream_op_batch send_message_;
    grpc_closure send_message_batch_done_ =
        MakeMemberClosure<Impl, &Impl::SendMessageBatchDone>(this);
    grpc_closure recv_message_batch_done_ =
        MakeMemberClosure<Impl, &Impl::RecvMessageBatchDone>(this);
    grpc_transport_stream_op_batch recv_message_;
    grpc_transport_stream_op_batch_payload batch_payload_{
        GetContext<grpc_call_context_element>()};
    grpc_closure stream_destroyed =
        MakeMemberClosure<Impl, &Impl::StreamDestroyed>(this);
  };

  Impl* impl_;
};

namespace {

template <ArenaPromise<ServerMetadataHandle> (*make_call_promise)(
    grpc_transport*, CallArgs)>
grpc_channel_filter MakeConnectedFilter() {
  return {
    connected_channel_start_transport_stream_op_batch,
        make_call_promise == nullptr
            ? nullptr
            : +[](grpc_channel_element* elem, CallArgs call_args,
                 NextPromiseFactory) {
                grpc_transport* transport =
                    static_cast<channel_data*>(elem->channel_data)->transport;
                return make_call_promise(transport, std::move(call_args));
              },
      connected_channel_start_transport_op,
      sizeof(call_data),
      connected_channel_init_call_elem,
      set_pollset_or_pollset_set,
      connected_channel_destroy_call_elem,
      sizeof(channel_data),
      connected_channel_init_channel_elem,
      +[](grpc_channel_stack* channel_stack, grpc_channel_element* elem) {
        /* HACK(ctiller): increase call stack size for the channel to make space
           for channel data. We need a cleaner (but performant) way to do this,
           and I'm not sure what that is yet.
           This is only "safe" because call stacks place no additional data
           after the last call element, and the last call element MUST be the
           connected channel. */
        channel_stack->call_stack_size += grpc_transport_stream_size(
            static_cast<channel_data*>(elem->channel_data)->transport);
      },
      connected_channel_destroy_channel_elem,
      connected_channel_get_channel_info,
      "connected",
  };
}

ArenaPromise<ServerMetadataHandle> MakeTransportCallPromise(
    grpc_transport* transport, CallArgs call_args) {
  return transport->vtable->make_call_promise(transport, std::move(call_args));
}

const grpc_channel_filter kPromiseBasedTransportFilter =
    MakeConnectedFilter<MakeTransportCallPromise>();

const grpc_channel_filter kClientEmulatedFilter =
    MakeConnectedFilter<ClientConnectedCallPromise::Make>();

const grpc_channel_filter kNoPromiseFilter = MakeConnectedFilter<nullptr>();

}  // namespace
}  // namespace grpc_core

bool grpc_add_connected_filter(grpc_core::ChannelStackBuilder* builder) {
  grpc_transport* t = builder->transport();
  GPR_ASSERT(t != nullptr);
  if (t->vtable->make_call_promise != nullptr) {
    builder->AppendFilter(&grpc_core::kPromiseBasedTransportFilter);
  } else if (grpc_channel_stack_type_is_client(builder->channel_stack_type())) {
    builder->AppendFilter(&grpc_core::kClientEmulatedFilter);
  } else {
    builder->AppendFilter(&grpc_core::kNoPromiseFilter);
  }
  return true;
}
