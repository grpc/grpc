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

#include <stdlib.h>

#include "channel_fwd.h"
#include "context.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/arena_promise.h"
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
      : transport_(transport),
        stream_(static_cast<grpc_stream*>(
            GetContext<Arena>()->Alloc(transport->vtable->sizeof_stream))) {
    grpc_transport_init_stream(transport_, stream_, nullptr, nullptr,
                               GetContext<Arena>());
  }

  static ArenaPromise<ServerMetadataHandle> Make(grpc_transport* transport,
                                                 CallArgs call_args) {
    return ClientConnectedCallPromise(transport, std::move(call_args));
  }

  Poll<ServerMetadataHandle> operator()() {
    if (!absl::exchange(requested_metadata_, true)) {
      memset(&metadata_, 0, sizeof(metadata_));
      metadata_.send_initial_metadata = true;
      metadata_.recv_initial_metadata = true;
      metadata_.recv_trailing_metadata = true;
      metadata_.payload = &batch_payload_;
      batch_payload_.send_initial_metadata.send_initial_metadata =
          client_initial_metadata_.get();
      // DO NOT SUBMIT: figure this field out
      batch_payload_.send_initial_metadata.send_initial_metadata_flags = 0;
      // DO NOT SUBMIT: figure this field out
      batch_payload_.send_initial_metadata.peer_string = nullptr;
      batch_payload_.recv_initial_metadata.recv_initial_metadata =
          &server_initial_metadata_;
      batch_payload_.recv_initial_metadata.recv_initial_metadata_ready =
          &recv_initial_metadata_ready_;
      // DO NOT SUBMIT: figure this field out
      batch_payload_.recv_initial_metadata.recv_flags = nullptr;
      // DO NOT SUBMIT: figure this field out
      batch_payload_.recv_initial_metadata.trailing_metadata_available =
          nullptr;
      // DO NOT SUBMIT: figure this field out
      batch_payload_.recv_initial_metadata.peer_string = nullptr;
      batch_payload_.recv_trailing_metadata.recv_trailing_metadata =
          &server_trailing_metadata_;
      batch_payload_.recv_trailing_metadata.recv_trailing_metadata_ready =
          &recv_trailing_metadata_ready_;
      push_metadata_ = true;
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
        if (p->has_value()) {
          send_message_state_ = std::move(*p);
          auto& msg = absl::get<Message>(send_message_state_);
          send_message_.send_message = true;
          batch_payload_.send_message.send_message = msg.payload();
          batch_payload_.send_message.flags = msg.flags();
        } else {
        }
      }
    }
    switch (send_message_state_) {
      case MessageState::kInTransport:
      case MessageState::kClosed:
        break;
      case MessageState::kGotResponse:
        client_to_server_messages_next_ = client_to_server_messages_->Next();
        ABSL_FALLTHROUGH_INTENDED;
      case MessageState::kIdle:
        break;
    }
    if (absl::exchange(queued_initial_metadata_, false)) {
      server_initial_metadata_latch_.Set(&server_initial_metadata_);
    }
    if (absl::exchange(queued_trailing_metadata_, false)) {
      return ServerMetadataHandle(&server_trailing_metadata_);
    }
    return Pending{};
  }

  void RecvInitialMetadataReady(grpc_error_handle error) {
    queued_initial_metadata_ = true;
    waker_.Wakeup();
  }

  void RecvTrailingMetadataReady(grpc_error_handle error) {
    queued_trailing_metadata_ = true;
    waker_.Wakeup();
  }

  void Push() {
    if (absl::exchange(push_metadata_, false)) {
      grpc_transport_perform_stream_op(transport_, stream_, &metadata_);
    }
    scheduled_push_ = false;
  }

 private:
  struct Idle {};
  struct Closed {};

  void SchedulePush() {
    if (absl::exchange(scheduled_push_, true)) return;
    ExecCtx::Run(DEBUG_LOCATION, &push_, GRPC_ERROR_NONE);
  }

  bool requested_metadata_ = false;
  bool push_metadata_ = false;
  bool scheduled_push_ = false;
  bool queued_initial_metadata_ = false;
  bool queued_trailing_metadata_ = false;
  MessageState send_message_state_ = MessageState::kIdle;
  Waker waker_;
  grpc_transport* const transport_;
  grpc_stream* const stream_;
  Latch<ServerMetadata*> server_initial_metadata_latch_;
  PipeReceiver<Message>* client_to_server_messages_;
  PipeSender<Message>* server_to_client_messages_;
  absl::variant<Idle, Closed, PipeReceiver<Message>::NextType, Message>
      send_message_state_;
  grpc_closure recv_initial_metadata_ready_ =
      MakeMemberClosure<ClientConnectedCallPromise,
                        &ClientConnectedCallPromise::RecvInitialMetadataReady>(
          this);
  grpc_closure recv_trailing_metadata_ready_ =
      MakeMemberClosure<ClientConnectedCallPromise,
                        &ClientConnectedCallPromise::RecvTrailingMetadataReady>(
          this);
  grpc_closure push_ =
      MakeMemberClosure<ClientConnectedCallPromise,
                        &ClientConnectedCallPromise::Push>(this);
  ClientMetadataHandle client_initial_metadata_;
  ServerMetadata server_initial_metadata_{GetContext<Arena>()};
  ServerMetadata server_trailing_metadata_{GetContext<Arena>()};
  grpc_transport_stream_op_batch metadata_;
  grpc_transport_stream_op_batch send_message_;
  grpc_transport_stream_op_batch recv_message_;
  grpc_transport_stream_op_batch_payload batch_payload_{
      GetContext<grpc_call_context_element>()};
};

namespace {

template <grpc_core::ArenaPromise<grpc_core::ServerMetadataHandle> (
    *make_call_promise)(grpc_transport*, grpc_core::CallArgs)>
grpc_channel_filter MakeConnectedFilter() {
  return {
    connected_channel_start_transport_stream_op_batch,
        make_call_promise == nullptr
            ? nullptr
            : [](grpc_channel_element* elem, CallArgs call_args,
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
      [](grpc_channel_stack* channel_stack, grpc_channel_element* elem) {
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
