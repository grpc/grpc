//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/channel/connected_channel.h"

#include <inttypes.h>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/race.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/batch_builder.h"
#include "src/core/lib/transport/error_utils.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

typedef struct connected_channel_channel_data {
  grpc_core::Transport* transport;
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
  GRPC_CALL_COMBINER_START(state->call_combiner, state->original_closure, error,
                           state->reason);
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

// We perform a small hack to locate transport data alongside the connected
// channel data in call allocations, to allow everything to be pulled in minimal
// cache line requests
#define TRANSPORT_STREAM_FROM_CALL_DATA(calld) \
  ((grpc_stream*)(((char*)(calld)) +           \
                  GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(call_data))))
#define CALL_DATA_FROM_TRANSPORT_STREAM(transport_stream) \
  ((call_data*)(((char*)(transport_stream)) -             \
                GPR_ROUND_UP_TO_ALIGNMENT_SIZE(sizeof(call_data))))

// Intercept a call operation and either push it directly up or translate it
// into transport stream operations
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
  chand->transport->filter_stack_transport()->PerformStreamOp(
      TRANSPORT_STREAM_FROM_CALL_DATA(calld), batch);
  GRPC_CALL_COMBINER_STOP(calld->call_combiner, "passed batch to transport");
}

static void connected_channel_start_transport_op(grpc_channel_element* elem,
                                                 grpc_transport_op* op) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->transport->PerformOp(op);
}

// Constructor for call_data
static grpc_error_handle connected_channel_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  calld->call_combiner = args->call_combiner;
  chand->transport->filter_stack_transport()->InitStream(
      TRANSPORT_STREAM_FROM_CALL_DATA(calld), &args->call_stack->refcount,
      args->server_transport_data, args->arena);
  return absl::OkStatus();
}

static void set_pollset_or_pollset_set(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->transport->SetPollingEntity(TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                     pollent);
}

// Destructor for call_data
static void connected_channel_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  chand->transport->filter_stack_transport()->DestroyStream(
      TRANSPORT_STREAM_FROM_CALL_DATA(calld), then_schedule_closure);
}

// Constructor for channel_data
static grpc_error_handle connected_channel_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(args->is_last);
  cd->transport = args->channel_args.GetObject<grpc_core::Transport>();
  return absl::OkStatus();
}

// Destructor for channel_data
static void connected_channel_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  if (cd->transport) {
    cd->transport->Orphan();
  }
}

// No-op.
static void connected_channel_get_channel_info(
    grpc_channel_element* /*elem*/, const grpc_channel_info* /*channel_info*/) {
}

namespace grpc_core {
namespace {

#if defined(GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_CLIENT_CALL) || \
    defined(GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL)
class ConnectedChannelStream : public Orphanable {
 public:
  explicit ConnectedChannelStream(Transport* transport)
      : transport_(transport), stream_(nullptr, StreamDeleter(this)) {
    GRPC_STREAM_REF_INIT(
        &stream_refcount_, 1,
        [](void* p, grpc_error_handle) {
          static_cast<ConnectedChannelStream*>(p)->BeginDestroy();
        },
        this, "ConnectedChannelStream");
  }

  Transport* transport() { return transport_; }
  grpc_closure* stream_destroyed_closure() { return &stream_destroyed_; }

  BatchBuilder::Target batch_target() {
    return BatchBuilder::Target{transport_, stream_.get(), &stream_refcount_};
  }

  void IncrementRefCount(const char* reason = "smartptr") {
#ifndef NDEBUG
    grpc_stream_ref(&stream_refcount_, reason);
#else
    (void)reason;
    grpc_stream_ref(&stream_refcount_);
#endif
  }

  void Unref(const char* reason = "smartptr") {
#ifndef NDEBUG
    grpc_stream_unref(&stream_refcount_, reason);
#else
    (void)reason;
    grpc_stream_unref(&stream_refcount_);
#endif
  }

  RefCountedPtr<ConnectedChannelStream> InternalRef() {
    IncrementRefCount("smartptr");
    return RefCountedPtr<ConnectedChannelStream>(this);
  }

  void Orphan() final {
    bool finished = finished_.IsSet();
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%s[connected] Orphan stream, finished: %d",
              party_->DebugTag().c_str(), finished);
    }
    // If we hadn't already observed the stream to be finished, we need to
    // cancel it at the transport.
    if (!finished) {
      party_->Spawn(
          "finish",
          [self = InternalRef()]() {
            if (!self->finished_.IsSet()) {
              self->finished_.Set();
            }
            return Empty{};
          },
          [](Empty) {});
      GetContext<BatchBuilder>()->Cancel(batch_target(),
                                         absl::CancelledError());
    }
    Unref("orphan connected stream");
  }

  // Returns a promise that implements the receive message loop.
  auto RecvMessages(PipeSender<MessageHandle>* incoming_messages,
                    bool cancel_on_error);
  // Returns a promise that implements the send message loop.
  auto SendMessages(PipeReceiver<MessageHandle>* outgoing_messages);

  void SetStream(grpc_stream* stream) { stream_.reset(stream); }
  grpc_stream* stream() { return stream_.get(); }
  grpc_stream_refcount* stream_refcount() { return &stream_refcount_; }

  void set_finished() { finished_.Set(); }
  auto WaitFinished() { return finished_.Wait(); }

 private:
  class StreamDeleter {
   public:
    explicit StreamDeleter(ConnectedChannelStream* impl) : impl_(impl) {}
    void operator()(grpc_stream* stream) const {
      if (stream == nullptr) return;
      impl_->transport()->filter_stack_transport()->DestroyStream(
          stream, impl_->stream_destroyed_closure());
    }

   private:
    ConnectedChannelStream* impl_;
  };
  using StreamPtr = std::unique_ptr<grpc_stream, StreamDeleter>;

  void StreamDestroyed() {
    call_context_->RunInContext([this] { this->~ConnectedChannelStream(); });
  }

  void BeginDestroy() {
    if (stream_ != nullptr) {
      stream_.reset();
    } else {
      StreamDestroyed();
    }
  }

  Transport* const transport_;
  RefCountedPtr<CallContext> const call_context_{
      GetContext<CallContext>()->Ref()};
  grpc_closure stream_destroyed_ =
      MakeMemberClosure<ConnectedChannelStream,
                        &ConnectedChannelStream::StreamDestroyed>(
          this, DEBUG_LOCATION);
  grpc_stream_refcount stream_refcount_;
  StreamPtr stream_;
  Arena* arena_ = GetContext<Arena>();
  Party* const party_ = GetContext<Party>();
  ExternallyObservableLatch<void> finished_;
};

auto ConnectedChannelStream::RecvMessages(
    PipeSender<MessageHandle>* incoming_messages, bool cancel_on_error) {
  return Loop([self = InternalRef(), cancel_on_error,
               incoming_messages = std::move(*incoming_messages)]() mutable {
    return Seq(
        GetContext<BatchBuilder>()->ReceiveMessage(self->batch_target()),
        [cancel_on_error, &incoming_messages](
            absl::StatusOr<absl::optional<MessageHandle>> status) mutable {
          bool has_message = status.ok() && status->has_value();
          auto publish_message = [&incoming_messages, &status]() {
            auto pending_message = std::move(**status);
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%s[connected] RecvMessage: received payload of %" PRIdPTR
                      " bytes",
                      GetContext<Activity>()->DebugTag().c_str(),
                      pending_message->payload()->Length());
            }
            return Map(incoming_messages.Push(std::move(pending_message)),
                       [](bool ok) -> LoopCtl<absl::Status> {
                         if (!ok) {
                           if (grpc_call_trace.enabled()) {
                             gpr_log(
                                 GPR_INFO,
                                 "%s[connected] RecvMessage: failed to "
                                 "push message towards the application",
                                 GetContext<Activity>()->DebugTag().c_str());
                           }
                           return absl::OkStatus();
                         }
                         return Continue{};
                       });
          };
          auto publish_close = [cancel_on_error, &incoming_messages,
                                &status]() mutable {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%s[connected] RecvMessage: reached end of stream with "
                      "status:%s",
                      GetContext<Activity>()->DebugTag().c_str(),
                      status.status().ToString().c_str());
            }
            if (cancel_on_error && !status.ok()) {
              incoming_messages.CloseWithError();
            } else {
              incoming_messages.Close();
            }
            return Immediate(LoopCtl<absl::Status>(status.status()));
          };
          return If(has_message, std::move(publish_message),
                    std::move(publish_close));
        });
  });
}

auto ConnectedChannelStream::SendMessages(
    PipeReceiver<MessageHandle>* outgoing_messages) {
  return ForEach(std::move(*outgoing_messages),
                 [self = InternalRef()](MessageHandle message) {
                   return GetContext<BatchBuilder>()->SendMessage(
                       self->batch_target(), std::move(message));
                 });
}
#endif  // defined(GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_CLIENT_CALL) ||
        // defined(GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL)

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_CLIENT_CALL
ArenaPromise<ServerMetadataHandle> MakeClientCallPromise(Transport* transport,
                                                         CallArgs call_args,
                                                         NextPromiseFactory) {
  OrphanablePtr<ConnectedChannelStream> stream(
      GetContext<Arena>()->New<ConnectedChannelStream>(transport));
  stream->SetStream(static_cast<grpc_stream*>(GetContext<Arena>()->Alloc(
      transport->filter_stack_transport()->SizeOfStream())));
  transport->filter_stack_transport()->InitStream(stream->stream(),
                                                  stream->stream_refcount(),
                                                  nullptr, GetContext<Arena>());
  auto* party = GetContext<Party>();
  party->Spawn("set_polling_entity", call_args.polling_entity->Wait(),
               [transport, stream = stream->InternalRef()](
                   grpc_polling_entity polling_entity) {
                 transport->SetPollingEntity(stream->stream(), &polling_entity);
               });
  // Start a loop to send messages from client_to_server_messages to the
  // transport. When the pipe closes and the loop completes, send a trailing
  // metadata batch to close the stream.
  party->Spawn(
      "send_messages",
      TrySeq(stream->SendMessages(call_args.client_to_server_messages),
             [stream = stream->InternalRef()]() {
               return GetContext<BatchBuilder>()->SendClientTrailingMetadata(
                   stream->batch_target());
             }),
      [](absl::Status) {});
  // Start a promise to receive server initial metadata and then forward it up
  // through the receiving pipe.
  auto server_initial_metadata =
      GetContext<Arena>()->MakePooled<ServerMetadata>(GetContext<Arena>());
  party->Spawn(
      "recv_initial_metadata",
      TrySeq(GetContext<BatchBuilder>()->ReceiveServerInitialMetadata(
                 stream->batch_target()),
             [pipe = call_args.server_initial_metadata](
                 ServerMetadataHandle server_initial_metadata) {
               if (grpc_call_trace.enabled()) {
                 gpr_log(GPR_DEBUG,
                         "%s[connected] Publish client initial metadata: %s",
                         GetContext<Activity>()->DebugTag().c_str(),
                         server_initial_metadata->DebugString().c_str());
               }
               return Map(pipe->Push(std::move(server_initial_metadata)),
                          [](bool r) {
                            if (r) return absl::OkStatus();
                            return absl::CancelledError();
                          });
             }),
      [](absl::Status) {});

  // Build up the rest of the main call promise:

  // Create a promise that will send initial metadata and then signal completion
  // of that via the token.
  auto send_initial_metadata = Seq(
      GetContext<BatchBuilder>()->SendClientInitialMetadata(
          stream->batch_target(), std::move(call_args.client_initial_metadata)),
      [sent_initial_metadata_token =
           std::move(call_args.client_initial_metadata_outstanding)](
          absl::Status status) mutable {
        sent_initial_metadata_token.Complete(status.ok());
        return status;
      });
  // Create a promise that will receive server trailing metadata.
  // If this fails, we massage the error into metadata that we can report
  // upwards.
  auto server_trailing_metadata =
      GetContext<Arena>()->MakePooled<ServerMetadata>(GetContext<Arena>());
  auto recv_trailing_metadata =
      Map(GetContext<BatchBuilder>()->ReceiveServerTrailingMetadata(
              stream->batch_target()),
          [](absl::StatusOr<ServerMetadataHandle> status) mutable {
            if (!status.ok()) {
              auto server_trailing_metadata =
                  GetContext<Arena>()->MakePooled<ServerMetadata>(
                      GetContext<Arena>());
              grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
              std::string message;
              grpc_error_get_status(status.status(), Timestamp::InfFuture(),
                                    &status_code, &message, nullptr, nullptr);
              server_trailing_metadata->Set(GrpcStatusMetadata(), status_code);
              server_trailing_metadata->Set(GrpcMessageMetadata(),
                                            Slice::FromCopiedString(message));
              return server_trailing_metadata;
            } else {
              return std::move(*status);
            }
          });
  // Finally the main call promise.
  // Concurrently: send initial metadata and receive messages, until BOTH
  // complete (or one fails).
  // Next: receive trailing metadata, and return that up the stack.
  auto recv_messages =
      stream->RecvMessages(call_args.server_to_client_messages, false);
  return Map(
      [send_initial_metadata = std::move(send_initial_metadata),
       recv_messages = std::move(recv_messages),
       recv_trailing_metadata = std::move(recv_trailing_metadata),
       done_send_initial_metadata = false, done_recv_messages = false,
       done_recv_trailing_metadata =
           false]() mutable -> Poll<ServerMetadataHandle> {
        if (!done_send_initial_metadata) {
          auto p = send_initial_metadata();
          if (auto* r = p.value_if_ready()) {
            done_send_initial_metadata = true;
            if (!r->ok()) return StatusCast<ServerMetadataHandle>(*r);
          }
        }
        if (!done_recv_messages) {
          auto p = recv_messages();
          if (p.ready()) {
            // NOTE: ignore errors here, they'll be collected in the
            // recv_trailing_metadata.
            done_recv_messages = true;
          } else {
            return Pending{};
          }
        }
        if (!done_recv_trailing_metadata) {
          auto p = recv_trailing_metadata();
          if (auto* r = p.value_if_ready()) {
            done_recv_trailing_metadata = true;
            return std::move(*r);
          }
        }
        return Pending{};
      },
      [stream = std::move(stream)](ServerMetadataHandle result) {
        stream->set_finished();
        return result;
      });
}
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL
ArenaPromise<ServerMetadataHandle> MakeServerCallPromise(
    Transport* transport, CallArgs, NextPromiseFactory next_promise_factory) {
  OrphanablePtr<ConnectedChannelStream> stream(
      GetContext<Arena>()->New<ConnectedChannelStream>(transport));

  stream->SetStream(static_cast<grpc_stream*>(GetContext<Arena>()->Alloc(
      transport->filter_stack_transport()->SizeOfStream())));
  transport->filter_stack_transport()->InitStream(
      stream->stream(), stream->stream_refcount(),
      GetContext<CallContext>()->server_call_context()->server_stream_data(),
      GetContext<Arena>());
  auto* party = GetContext<Party>();

  // Arifacts we need for the lifetime of the call.
  struct CallData {
    Pipe<MessageHandle> server_to_client;
    Pipe<MessageHandle> client_to_server;
    Pipe<ServerMetadataHandle> server_initial_metadata;
    Latch<ServerMetadataHandle> failure_latch;
    Latch<grpc_polling_entity> polling_entity_latch;
    bool sent_initial_metadata = false;
    bool sent_trailing_metadata = false;
  };
  auto* call_data = GetContext<Arena>()->New<CallData>();
  GetContext<CallFinalization>()->Add(
      [call_data](const grpc_call_final_info*) { call_data->~CallData(); });

  party->Spawn("set_polling_entity", call_data->polling_entity_latch.Wait(),
               [transport, stream = stream->InternalRef()](
                   grpc_polling_entity polling_entity) {
                 transport->SetPollingEntity(stream->stream(), &polling_entity);
               });

  auto server_to_client_empty =
      call_data->server_to_client.receiver.AwaitEmpty();

  // Create a promise that will receive client initial metadata, and then run
  // the main stem of the call (calling next_promise_factory up through the
  // filters).
  // Race the main call with failure_latch, allowing us to forcefully complete
  // the call in the case of a failure.
  auto recv_initial_metadata_then_run_promise =
      TrySeq(GetContext<BatchBuilder>()->ReceiveClientInitialMetadata(
                 stream->batch_target()),
             [next_promise_factory = std::move(next_promise_factory),
              server_to_client_empty = std::move(server_to_client_empty),
              call_data](ClientMetadataHandle client_initial_metadata) {
               auto call_promise = next_promise_factory(CallArgs{
                   std::move(client_initial_metadata),
                   ClientInitialMetadataOutstandingToken::Empty(),
                   &call_data->polling_entity_latch,
                   &call_data->server_initial_metadata.sender,
                   &call_data->client_to_server.receiver,
                   &call_data->server_to_client.sender,
               });
               return Race(call_data->failure_latch.Wait(),
                           [call_promise = std::move(call_promise),
                            server_to_client_empty =
                                std::move(server_to_client_empty)]() mutable
                           -> Poll<ServerMetadataHandle> {
                             // TODO(ctiller): this is deeply weird and we need
                             // to clean this up.
                             //
                             // The following few lines check to ensure that
                             // there's no message currently pending in the
                             // outgoing message queue, and if (and only if)
                             // that's true decides to poll the main promise to
                             // see if there's a result.
                             //
                             // This essentially introduces a polling priority
                             // scheme that makes the current promise structure
                             // work out the way we want when talking to
                             // transports.
                             //
                             // The problem is that transports are going to need
                             // to replicate this structure when they convert to
                             // promises, and that becomes troubling as we'll be
                             // replicating weird throughout the stack.
                             //
                             // Instead we likely need to change the way we're
                             // composing promises through the stack.
                             //
                             // Proposed is to change filters from a promise
                             // that takes ClientInitialMetadata and returns
                             // ServerTrailingMetadata with three pipes for
                             // ServerInitialMetadata and
                             // ClientToServerMessages, ServerToClientMessages.
                             // Instead we'll have five pipes, moving
                             // ClientInitialMetadata and ServerTrailingMetadata
                             // to pipes that can be intercepted.
                             //
                             // The effect of this change will be to cripple the
                             // things that can be done in a filter (but cripple
                             // in line with what most filters actually do).
                             // We'll likely need to add a `CallContext::Cancel`
                             // to allow filters to cancel a request, but this
                             // would also have the advantage of centralizing
                             // our cancellation machinery which seems like an
                             // additional win - with the net effect that the
                             // shape of the call gets made explicit at the top
                             // & bottom of the stack.
                             //
                             // There's a small set of filters (retry, this one,
                             // lame client, clinet channel) that terminate
                             // stacks and need a richer set of semantics, but
                             // that ends up being fine because we can spawn
                             // tasks in parties to handle those edge cases, and
                             // keep the majority of filters simple: they just
                             // call InterceptAndMap on a handful of filters at
                             // call initialization time and then proceed to
                             // actually filter.
                             //
                             // So that's the plan, why isn't it enacted here?
                             //
                             // Well, the plan ends up being easy to implement
                             // in the promise based world (I did a prototype on
                             // a branch in an afternoon). It's heinous to
                             // implement in promise_based_filter, and that code
                             // is load bearing for us at the time of writing.
                             // It's not worth delaying promises for a further N
                             // months (N ~ 6) to make that change.
                             //
                             // Instead, we'll move forward with this, get
                             // promise_based_filter out of the picture, and
                             // then during the mop-up phase for promises tweak
                             // the compute structure to move to the magical
                             // five pipes (I'm reminded of an old Onion
                             // article), and end up in a good happy place.
                             if (server_to_client_empty().pending()) {
                               return Pending{};
                             }
                             return call_promise();
                           });
             });

  // Promise factory that accepts a ServerMetadataHandle, and sends it as the
  // trailing metadata for this call.
  auto send_trailing_metadata = [call_data, stream = stream->InternalRef()](
                                    ServerMetadataHandle
                                        server_trailing_metadata) {
    bool is_cancellation =
        server_trailing_metadata->get(GrpcCallWasCancelled()).value_or(false);
    return GetContext<BatchBuilder>()->SendServerTrailingMetadata(
        stream->batch_target(), std::move(server_trailing_metadata),
        is_cancellation ||
            !std::exchange(call_data->sent_initial_metadata, true));
  };

  // Runs the receive message loop, either until all the messages
  // are received or the server call is complete.
  party->Spawn(
      "recv_messages",
      Race(
          Map(stream->WaitFinished(), [](Empty) { return absl::OkStatus(); }),
          Map(stream->RecvMessages(&call_data->client_to_server.sender, true),
              [failure_latch = &call_data->failure_latch](absl::Status status) {
                if (!status.ok() && !failure_latch->is_set()) {
                  failure_latch->Set(ServerMetadataFromStatus(status));
                }
                return status;
              })),
      [](absl::Status) {});

  // Run a promise that will send initial metadata (if that pipe sends some).
  // And then run the send message loop until that completes.

  auto send_initial_metadata = Seq(
      Race(Map(stream->WaitFinished(),
               [](Empty) { return NextResult<ServerMetadataHandle>(true); }),
           call_data->server_initial_metadata.receiver.Next()),
      [call_data, stream = stream->InternalRef()](
          NextResult<ServerMetadataHandle> next_result) mutable {
        auto md = !call_data->sent_initial_metadata && next_result.has_value()
                      ? std::move(next_result.value())
                      : nullptr;
        if (md != nullptr) {
          call_data->sent_initial_metadata = true;
          auto* party = GetContext<Party>();
          party->Spawn("connected/send_initial_metadata",
                       GetContext<BatchBuilder>()->SendServerInitialMetadata(
                           stream->batch_target(), std::move(md)),
                       [](absl::Status) {});
          return Immediate(absl::OkStatus());
        }
        return Immediate(absl::CancelledError());
      });
  party->Spawn(
      "send_initial_metadata_then_messages",
      Race(Map(stream->WaitFinished(), [](Empty) { return absl::OkStatus(); }),
           TrySeq(std::move(send_initial_metadata),
                  stream->SendMessages(&call_data->server_to_client.receiver))),
      [](absl::Status) {});

  // Spawn a job to fetch the "client trailing metadata" - if this is OK then
  // it's client done, otherwise it's a signal of cancellation from the client
  // which we'll use failure_latch to signal.

  party->Spawn(
      "recv_trailing_metadata",
      Seq(GetContext<BatchBuilder>()->ReceiveClientTrailingMetadata(
              stream->batch_target()),
          [failure_latch = &call_data->failure_latch](
              absl::StatusOr<ClientMetadataHandle> status) mutable {
            if (grpc_call_trace.enabled()) {
              gpr_log(
                  GPR_DEBUG,
                  "%s[connected] Got trailing metadata; status=%s metadata=%s",
                  GetContext<Activity>()->DebugTag().c_str(),
                  status.status().ToString().c_str(),
                  status.ok() ? (*status)->DebugString().c_str() : "<none>");
            }
            ClientMetadataHandle trailing_metadata;
            if (status.ok()) {
              trailing_metadata = std::move(*status);
            } else {
              trailing_metadata =
                  GetContext<Arena>()->MakePooled<ClientMetadata>(
                      GetContext<Arena>());
              grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
              std::string message;
              grpc_error_get_status(status.status(), Timestamp::InfFuture(),
                                    &status_code, &message, nullptr, nullptr);
              trailing_metadata->Set(GrpcStatusMetadata(), status_code);
              trailing_metadata->Set(GrpcMessageMetadata(),
                                     Slice::FromCopiedString(message));
            }
            if (trailing_metadata->get(GrpcStatusMetadata())
                    .value_or(GRPC_STATUS_UNKNOWN) != GRPC_STATUS_OK) {
              if (!failure_latch->is_set()) {
                failure_latch->Set(std::move(trailing_metadata));
              }
            }
            return Empty{};
          }),
      [](Empty) {});

  // Finally assemble the main call promise:
  // Receive initial metadata from the client and start the promise up the
  // filter stack.
  // Upon completion, send trailing metadata to the client and then return it
  // (allowing the call code to decide on what signalling to give the
  // application).

  struct CleanupPollingEntityLatch {
    void operator()(Latch<grpc_polling_entity>* latch) {
      if (!latch->is_set()) latch->Set(grpc_polling_entity());
    }
  };
  auto cleanup_polling_entity_latch =
      std::unique_ptr<Latch<grpc_polling_entity>, CleanupPollingEntityLatch>(
          &call_data->polling_entity_latch);
  struct CleanupSendInitialMetadata {
    void operator()(CallData* call_data) {
      call_data->server_initial_metadata.receiver.CloseWithError();
    }
  };
  auto cleanup_send_initial_metadata =
      std::unique_ptr<CallData, CleanupSendInitialMetadata>(call_data);

  return Map(
      Seq(std::move(recv_initial_metadata_then_run_promise),
          std::move(send_trailing_metadata)),
      [cleanup_polling_entity_latch = std::move(cleanup_polling_entity_latch),
       cleanup_send_initial_metadata = std::move(cleanup_send_initial_metadata),
       stream = std::move(stream)](ServerMetadataHandle md) {
        stream->set_finished();
        return md;
      });
}
#endif

template <ArenaPromise<ServerMetadataHandle> (*make_call_promise)(
    Transport*, CallArgs, NextPromiseFactory)>
grpc_channel_filter MakeConnectedFilter() {
  // Create a vtable that contains both the legacy call methods (for filter
  // stack based calls) and the new promise based method for creating
  // promise based calls (the latter iff make_call_promise != nullptr). In
  // this way the filter can be inserted into either kind of channel stack,
  // and only if all the filters in the stack are promise based will the
  // call be promise based.
  auto make_call_wrapper = +[](grpc_channel_element* elem, CallArgs call_args,
                               NextPromiseFactory next) {
    Transport* transport =
        static_cast<channel_data*>(elem->channel_data)->transport;
    return make_call_promise(transport, std::move(call_args), std::move(next));
  };
  return {
      connected_channel_start_transport_stream_op_batch,
      make_call_promise != nullptr ? make_call_wrapper : nullptr,
      /* init_call: */ nullptr,
      connected_channel_start_transport_op,
      sizeof(call_data),
      connected_channel_init_call_elem,
      set_pollset_or_pollset_set,
      connected_channel_destroy_call_elem,
      sizeof(channel_data),
      connected_channel_init_channel_elem,
      +[](grpc_channel_stack* channel_stack, grpc_channel_element* elem) {
        // HACK(ctiller): increase call stack size for the channel to make
        // space for channel data. We need a cleaner (but performant) way to
        // do this, and I'm not sure what that is yet. This is only "safe"
        // because call stacks place no additional data after the last call
        // element, and the last call element MUST be the connected channel.
        auto* transport =
            static_cast<channel_data*>(elem->channel_data)->transport;
        if (transport->filter_stack_transport() != nullptr) {
          channel_stack->call_stack_size +=
              transport->filter_stack_transport()->SizeOfStream();
        }
      },
      connected_channel_destroy_channel_elem,
      connected_channel_get_channel_info,
      "connected",
  };
}

ArenaPromise<ServerMetadataHandle> MakeClientTransportCallPromise(
    Transport* transport, CallArgs call_args, NextPromiseFactory) {
  auto spine = GetContext<CallContext>()->MakeCallSpine(std::move(call_args));
  transport->client_transport()->StartCall(CallHandler{spine});
  return Map(spine->server_trailing_metadata().receiver.Next(),
             [](NextResult<ServerMetadataHandle> r) {
               if (r.has_value()) {
                 auto md = std::move(r.value());
                 md->Set(GrpcStatusFromWire(), true);
                 return md;
               }
               auto m = GetContext<Arena>()->MakePooled<ServerMetadata>(
                   GetContext<Arena>());
               m->Set(GrpcStatusMetadata(), GRPC_STATUS_CANCELLED);
               m->Set(GrpcCallWasCancelled(), true);
               return m;
             });
}

const grpc_channel_filter kClientPromiseBasedTransportFilter =
    MakeConnectedFilter<MakeClientTransportCallPromise>();

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_CLIENT_CALL
const grpc_channel_filter kClientEmulatedFilter =
    MakeConnectedFilter<MakeClientCallPromise>();
#else
const grpc_channel_filter kClientEmulatedFilter =
    MakeConnectedFilter<nullptr>();
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL
const grpc_channel_filter kServerEmulatedFilter =
    MakeConnectedFilter<MakeServerCallPromise>();
#else
const grpc_channel_filter kServerEmulatedFilter =
    MakeConnectedFilter<nullptr>();
#endif

// noop filter for the v3 stack: placeholder for now because other code requires
// we have a terminator.
// TODO(ctiller): delete when v3 transition is complete.
const grpc_channel_filter kServerPromiseBasedTransportFilter = {
    nullptr,
    [](grpc_channel_element*, CallArgs, NextPromiseFactory)
        -> ArenaPromise<ServerMetadataHandle> { Crash("not implemented"); },
    /* init_call: */ [](grpc_channel_element*, CallSpineInterface*) {},
    connected_channel_start_transport_op,
    0,
    nullptr,
    set_pollset_or_pollset_set,
    nullptr,
    sizeof(channel_data),
    connected_channel_init_channel_elem,
    +[](grpc_channel_stack*, grpc_channel_element*) {},
    connected_channel_destroy_channel_elem,
    connected_channel_get_channel_info,
    "connected",
};

bool TransportSupportsClientPromiseBasedCalls(const ChannelArgs& args) {
  auto* transport = args.GetObject<Transport>();
  return transport->client_transport() != nullptr;
}

bool TransportSupportsServerPromiseBasedCalls(const ChannelArgs& args) {
  auto* transport = args.GetObject<Transport>();
  return transport->server_transport() != nullptr;
}

}  // namespace

void RegisterConnectedChannel(CoreConfiguration::Builder* builder) {
  // We can't know promise based call or not here (that decision needs the
  // collaboration of all of the filters on the channel, and we don't want
  // ordering constraints on when we add filters).
  // We can know if this results in a promise based call how we'll create
  // our promise (if indeed we can), and so that is the choice made here.

  // Option 1, and our ideal: the transport supports promise based calls,
  // and so we simply use the transport directly.
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL,
                       &kClientPromiseBasedTransportFilter)
      .Terminal()
      .If(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_DIRECT_CHANNEL,
                       &kClientPromiseBasedTransportFilter)
      .Terminal()
      .If(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_SERVER_CHANNEL, &kServerPromiseBasedTransportFilter)
      .Terminal()
      .If(TransportSupportsServerPromiseBasedCalls);

  // Option 2: the transport does not support promise based calls.
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_SUBCHANNEL, &kClientEmulatedFilter)
      .Terminal()
      .IfNot(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_CLIENT_DIRECT_CHANNEL, &kClientEmulatedFilter)
      .Terminal()
      .IfNot(TransportSupportsClientPromiseBasedCalls);
  builder->channel_init()
      ->RegisterFilter(GRPC_SERVER_CHANNEL, &kServerEmulatedFilter)
      .Terminal()
      .IfNot(TransportSupportsServerPromiseBasedCalls);
}

}  // namespace grpc_core
