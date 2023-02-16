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
#include <string.h>

#include <atomic>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gpr/alloc.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/detail/basic_seq.h"
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
#include "src/core/lib/promise/try_join.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/error_utils.h"
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
  grpc_transport_perform_stream_op(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld), batch);
  GRPC_CALL_COMBINER_STOP(calld->call_combiner, "passed batch to transport");
}

static void connected_channel_start_transport_op(grpc_channel_element* elem,
                                                 grpc_transport_op* op) {
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_perform_op(chand->transport, op);
}

// Constructor for call_data
static grpc_error_handle connected_channel_init_call_elem(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  calld->call_combiner = args->call_combiner;
  int r = grpc_transport_init_stream(
      chand->transport, TRANSPORT_STREAM_FROM_CALL_DATA(calld),
      &args->call_stack->refcount, args->server_transport_data, args->arena);
  return r == 0 ? absl::OkStatus()
                : GRPC_ERROR_CREATE("transport stream initialization failed");
}

static void set_pollset_or_pollset_set(grpc_call_element* elem,
                                       grpc_polling_entity* pollent) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_set_pops(chand->transport,
                          TRANSPORT_STREAM_FROM_CALL_DATA(calld), pollent);
}

// Destructor for call_data
static void connected_channel_destroy_call_elem(
    grpc_call_element* elem, const grpc_call_final_info* /*final_info*/,
    grpc_closure* then_schedule_closure) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  channel_data* chand = static_cast<channel_data*>(elem->channel_data);
  grpc_transport_destroy_stream(chand->transport,
                                TRANSPORT_STREAM_FROM_CALL_DATA(calld),
                                then_schedule_closure);
}

// Constructor for channel_data
static grpc_error_handle connected_channel_init_channel_elem(
    grpc_channel_element* elem, grpc_channel_element_args* args) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  GPR_ASSERT(args->is_last);
  cd->transport = args->channel_args.GetObject<grpc_transport>();
  return absl::OkStatus();
}

// Destructor for channel_data
static void connected_channel_destroy_channel_elem(grpc_channel_element* elem) {
  channel_data* cd = static_cast<channel_data*>(elem->channel_data);
  if (cd->transport) {
    grpc_transport_destroy(cd->transport);
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
  explicit ConnectedChannelStream(grpc_transport* transport)
      : transport_(transport), stream_(nullptr, StreamDeleter(this)) {
    call_context_->IncrementRefCount("connected_channel_stream");
    GRPC_STREAM_REF_INIT(
        &stream_refcount_, 1,
        [](void* p, grpc_error_handle) {
          static_cast<ConnectedChannelStream*>(p)->BeginDestroy();
        },
        this, "client_stream");
  }

  grpc_transport* transport() { return transport_; }
  grpc_closure* stream_destroyed_closure() { return &stream_destroyed_; }

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
    IncrementRefCount("internal_ref");
    return RefCountedPtr<ConnectedChannelStream>(this);
  }

  void Orphan() final {
    bool finished = finished_.load(std::memory_order_acquire);
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%s[connected] Orphan stream, finished: %d",
              party_->DebugTag().c_str(), finished);
    }
    // If we hadn't already observed the stream to be finished, we need to
    // cancel it at the transport.
    if (!finished) {
      IncrementRefCount("shutdown client stream");
      auto* cancel_op =
          GetContext<Arena>()->New<grpc_transport_stream_op_batch>();
      cancel_op->cancel_stream = true;
      cancel_op->payload = &batch_payload_;
      cancel_op->on_complete = NewClosure(
          [this](grpc_error_handle) { Unref("shutdown client stream"); });
      batch_payload_.cancel_stream.cancel_error = absl::CancelledError();
      grpc_transport_perform_stream_op(transport_, stream_.get(), cancel_op);
    }
    Unref("orphan client stream");
  }

  template <typename F>
  auto PushBatchToTransport(absl::string_view name, F configurator);

  auto RecvMessages(PipeSender<MessageHandle>* incoming_messages);
  auto SendMessages(PipeReceiver<MessageHandle>* outgoing_messages);

  void SetStream(grpc_stream* stream) { stream_.reset(stream); }
  grpc_stream* stream() { return stream_.get(); }
  grpc_stream_refcount* stream_refcount() { return &stream_refcount_; }

  void set_finished() { finished_.store(true, std::memory_order_relaxed); }

 private:
  struct Batch {
    struct Done {
      Arena::PoolPtr<Batch> batch;
      absl::Status status;
    };
    grpc_transport_stream_op_batch batch;
    grpc_closure on_done_closure;
    ConnectedChannelStream* stream;
    Arena::PoolPtr<Batch> self;
    Latch<Done> done;
    absl::string_view name;
  };

  struct PendingReceiveMessage {
    absl::optional<SliceBuffer> payload;
    uint32_t flags;

    void ConfigureBatch(grpc_transport_stream_op_batch* batch,
                        grpc_closure* on_done) {
      batch->recv_message = true;
      batch->payload->recv_message.recv_message = &payload;
      batch->payload->recv_message.flags = &flags;
      batch->payload->recv_message.call_failed_before_recv_message = nullptr;
      batch->payload->recv_message.recv_message_ready = on_done;
    }

    MessageHandle IntoMessageHandle() {
      return GetContext<Arena>()->MakePooled<Message>(std::move(*payload),
                                                      flags);
    }
  };

  class StreamDeleter {
   public:
    explicit StreamDeleter(ConnectedChannelStream* impl) : impl_(impl) {}
    void operator()(grpc_stream* stream) const {
      if (stream == nullptr) return;
      grpc_transport_destroy_stream(impl_->transport(), stream,
                                    impl_->stream_destroyed_closure());
    }

   private:
    ConnectedChannelStream* impl_;
  };
  using StreamPtr = std::unique_ptr<grpc_stream, StreamDeleter>;

  void StreamDestroyed() {
    call_context_->RunInContext([this] {
      auto* cc = call_context_;
      this->~ConnectedChannelStream();
      cc->Unref("child_stream");
    });
  }

  void BeginDestroy() {
    if (stream_ != nullptr) {
      stream_.reset();
    } else {
      StreamDestroyed();
    }
  }

  Batch* MakeBatch(absl::string_view name) {
    auto batch = GetContext<Arena>()->MakePooled<Batch>();
    memset(&batch->batch, 0, sizeof(batch->batch));
    batch->stream = this;
    batch->name = name;
    batch->batch.payload = &batch_payload_;
    GRPC_CLOSURE_INIT(
        &batch->on_done_closure,
        [](void* arg, grpc_error_handle status) {
          auto* batch = static_cast<Batch*>(arg);
          auto name = batch->name;
          auto* stream = batch->stream;
          auto* party = stream->party_;
          if (grpc_call_trace.enabled()) {
            gpr_log(
                GPR_DEBUG, "%s[connected] Finish batch '%s' %s: status=%s",
                party->DebugTag().c_str(), std::string(batch->name).c_str(),
                grpc_transport_stream_op_batch_string(&batch->batch).c_str(),
                status.ToString().c_str());
          }
          party->Spawn(
              name,
              [batch, status = std::move(status)]() mutable {
                batch->done.Set(
                    Batch::Done{std::move(batch->self), std::move(status)});
                return Empty{};
              },
              [stream](Empty) { stream->Unref("push batch"); });
        },
        batch.get(), nullptr);
    Batch* b = batch.get();
    batch->self = std::move(batch);
    return b;
  }

  auto PushBatch(Batch* b) {
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%s[connected] Start batch '%s' %s",
              b->stream->party_->DebugTag().c_str(),
              std::string(b->name).c_str(),
              grpc_transport_stream_op_batch_string(&b->batch).c_str());
    }
    GPR_ASSERT(b->batch.HasOp());
    IncrementRefCount("push batch");
    grpc_transport_perform_stream_op(transport_, stream_.get(), &b->batch);
    return Map(b->done.Wait(),
               [](Batch::Done d) { return std::move(d.status); });
  }

  grpc_transport* const transport_;
  CallContext* const call_context_{GetContext<CallContext>()};
  grpc_closure stream_destroyed_ =
      MakeMemberClosure<ConnectedChannelStream,
                        &ConnectedChannelStream::StreamDestroyed>(
          this, DEBUG_LOCATION);
  grpc_stream_refcount stream_refcount_;
  StreamPtr stream_;
  Arena* arena_ = GetContext<Arena>();
  Party* const party_ = static_cast<Party*>(Activity::current());
  std::atomic<bool> finished_{false};

  grpc_transport_stream_op_batch_payload batch_payload_{
      GetContext<grpc_call_context_element>()};
};

template <typename F>
auto ConnectedChannelStream::PushBatchToTransport(absl::string_view name,
                                                  F configurator) {
  Batch* b = MakeBatch(name);
  configurator(&b->batch, &b->on_done_closure);
  return PushBatch(b);
}

auto ConnectedChannelStream::RecvMessages(
    PipeSender<MessageHandle>* incoming_messages) {
  return Loop([this,
               incoming_messages = std::move(*incoming_messages)]() mutable {
    auto pending_message =
        GetContext<Arena>()->MakePooled<PendingReceiveMessage>();
    auto* pm = pending_message.get();
    return Seq(
        PushBatchToTransport(
            "recv_message",
            [pm](grpc_transport_stream_op_batch* batch, grpc_closure* on_done) {
              pm->ConfigureBatch(batch, on_done);
            }),
        [&incoming_messages, pending_message = std::move(pending_message)](
            absl::Status status) mutable {
          bool has_message =
              status.ok() && pending_message->payload.has_value();
          auto publish_message = [&incoming_messages,
                                  pending_message =
                                      std::move(pending_message)]() {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%s[connected] RecvMessage: received payload of %" PRIdPTR
                      " bytes",
                      Activity::current()->DebugTag().c_str(),
                      pending_message->payload->Length());
            }
            return Map(
                incoming_messages.Push(pending_message->IntoMessageHandle()),
                [](bool ok) -> LoopCtl<absl::Status> {
                  if (!ok) {
                    if (grpc_call_trace.enabled()) {
                      gpr_log(GPR_INFO,
                              "%s[connected] RecvMessage: failed to "
                              "push message towards the application",
                              Activity::current()->DebugTag().c_str());
                    }
                    return absl::OkStatus();
                  }
                  return Continue{};
                });
          };
          auto publish_close =
              [status = std::move(status)]() mutable -> LoopCtl<absl::Status> {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%s[connected] RecvMessage: reached end of stream with "
                      "status:%s",
                      Activity::current()->DebugTag().c_str(),
                      status.ToString().c_str());
            }
            return std::move(status);
          };
          return If(has_message, std::move(publish_message),
                    std::move(publish_close));
        });
  });
}

auto ConnectedChannelStream::SendMessages(
    PipeReceiver<MessageHandle>* outgoing_messages) {
  return ForEach(std::move(*outgoing_messages), [this](MessageHandle message) {
    auto done = PushBatchToTransport(
        "send_message",
        [message = message.get()](grpc_transport_stream_op_batch* batch,
                                  grpc_closure* on_done) {
          batch->send_message = true;
          batch->payload->send_message.send_message = message->payload();
          batch->payload->send_message.flags = message->flags();
          batch->on_complete = on_done;
        });
    return Map(done, [message = std::move(message)](absl::Status status) {
      return status;
    });
  });
}
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_CLIENT_CALL
ArenaPromise<ServerMetadataHandle> MakeClientCallPromise(
    grpc_transport* transport, CallArgs call_args, NextPromiseFactory) {
  OrphanablePtr<ConnectedChannelStream> stream(
      GetContext<Arena>()->New<ConnectedChannelStream>(transport));
  stream->SetStream(static_cast<grpc_stream*>(
      GetContext<Arena>()->Alloc(transport->vtable->sizeof_stream)));
  grpc_transport_init_stream(transport, stream->stream(),
                             stream->stream_refcount(), nullptr,
                             GetContext<Arena>());
  grpc_transport_set_pops(transport, stream->stream(),
                          GetContext<CallContext>()->polling_entity());
  auto* party = static_cast<Party*>(Activity::current());
  party->Spawn(
      "send_messages",
      TrySeq(stream->SendMessages(call_args.client_to_server_messages),
             [stream = stream->InternalRef()]() {
               return stream->PushBatchToTransport(
                   "close_sends", [](grpc_transport_stream_op_batch* batch,
                                     grpc_closure* on_done) {
                     batch->send_trailing_metadata = true;
                     batch->on_complete = on_done;
                     batch->payload->send_trailing_metadata
                         .send_trailing_metadata =
                         GetContext<Arena>()->ManagedNew<ClientMetadata>(
                             GetContext<Arena>());
                     batch->payload->send_trailing_metadata.sent = nullptr;
                   });
             }),
      [](absl::Status) {});
  auto server_initial_metadata =
      GetContext<Arena>()->MakePooled<ServerMetadata>(GetContext<Arena>(),
                                                      DEBUG_LOCATION);
  party->Spawn(
      "recv_initial_metadata",
      TrySeq(stream->PushBatchToTransport(
                 "recv_initial_metadata_batch",
                 [server_initial_metadata = server_initial_metadata.get()](
                     grpc_transport_stream_op_batch* batch,
                     grpc_closure* on_done) {
                   batch->recv_initial_metadata = true;
                   batch->payload->recv_initial_metadata.recv_initial_metadata =
                       server_initial_metadata;
                   batch->payload->recv_initial_metadata
                       .recv_initial_metadata_ready = on_done;
                   batch->payload->recv_initial_metadata
                       .trailing_metadata_available = nullptr;
                   batch->payload->recv_initial_metadata.peer_string = nullptr;
                 }),
             [pipe = call_args.server_initial_metadata,
              server_initial_metadata =
                  std::move(server_initial_metadata)]() mutable {
               return Map(pipe->Push(std::move(server_initial_metadata)),
                          [](bool r) {
                            if (r) return absl::OkStatus();
                            return absl::CancelledError();
                          });
             }),
      [](absl::Status) {});
  auto* client_initial_metadata = call_args.client_initial_metadata.get();
  auto send_initial_metadata = Seq(
      stream->PushBatchToTransport(
          "send_initial_metadata_batch",
          [client_initial_metadata](grpc_transport_stream_op_batch* batch,
                                    grpc_closure* on_done) {
            batch->send_initial_metadata = true;
            batch->payload->send_initial_metadata.send_initial_metadata =
                client_initial_metadata;
            batch->payload->send_initial_metadata.peer_string =
                GetContext<CallContext>()->peer_string_atm_ptr();
            batch->on_complete = on_done;
          }),
      [client_initial_metadata = std::move(call_args.client_initial_metadata),
       sent_initial_metadata_token =
           std::move(call_args.client_initial_metadata_outstanding)](
          absl::Status status) mutable {
        sent_initial_metadata_token.Complete(status.ok());
        return status;
      });
  auto server_trailing_metadata =
      GetContext<Arena>()->MakePooled<ServerMetadata>(GetContext<Arena>(),
                                                      DEBUG_LOCATION);
  auto recv_trailing_metadata = Map(
      stream->PushBatchToTransport(
          "recv_trailing_metadata",
          [server_trailing_metadata = server_trailing_metadata.get()](
              grpc_transport_stream_op_batch* batch, grpc_closure* on_done) {
            batch->recv_trailing_metadata = true;
            batch->payload->recv_trailing_metadata.recv_trailing_metadata =
                server_trailing_metadata;
            batch->payload->recv_trailing_metadata.collect_stats =
                &GetContext<CallContext>()
                     ->call_stats()
                     ->transport_stream_stats;
            batch->payload->recv_trailing_metadata
                .recv_trailing_metadata_ready = on_done;
          }),
      [server_trailing_metadata =
           std::move(server_trailing_metadata)](absl::Status status) mutable {
        if (!status.ok()) {
          server_trailing_metadata->Clear();
          grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
          std::string message;
          grpc_error_get_status(status, Timestamp::InfFuture(), &status_code,
                                &message, nullptr, nullptr);
          server_trailing_metadata->Set(GrpcStatusMetadata(), status_code);
          server_trailing_metadata->Set(GrpcMessageMetadata(),
                                        Slice::FromCopiedString(message));
        }
        return std::move(server_trailing_metadata);
      });
  return Map(
      TrySeq(TryJoin(std::move(send_initial_metadata),
                     stream->RecvMessages(call_args.server_to_client_messages)),
             std::move(recv_trailing_metadata)),
      [stream = std::move(stream)](ServerMetadataHandle result) {
        return result;
      });
}
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL
ArenaPromise<ServerMetadataHandle> MakeServerCallPromise(
    grpc_transport* transport, CallArgs,
    NextPromiseFactory next_promise_factory) {
  OrphanablePtr<ConnectedChannelStream> stream(
      GetContext<Arena>()->New<ConnectedChannelStream>(transport));

  stream->SetStream(static_cast<grpc_stream*>(
      GetContext<Arena>()->Alloc(transport->vtable->sizeof_stream)));
  grpc_transport_init_stream(
      transport, stream->stream(), stream->stream_refcount(),
      GetContext<CallContext>()->server_call_context()->server_stream_data(),
      GetContext<Arena>());
  grpc_transport_set_pops(transport, stream->stream(),
                          GetContext<CallContext>()->polling_entity());

  auto* party = static_cast<Party*>(Activity::current());

  struct CallData {
    Pipe<MessageHandle> server_to_client;
    Pipe<MessageHandle> client_to_server;
    Pipe<ServerMetadataHandle> server_initial_metadata;
    Latch<ServerMetadataHandle> failure_latch;
    bool sent_initial_metadata = false;
    bool sent_trailing_metadata = false;
  };

  auto* call_data = GetContext<Arena>()->ManagedNew<CallData>();

  auto client_initial_metadata =
      GetContext<Arena>()->MakePooled<ClientMetadata>(GetContext<Arena>());
  auto recv_initial_metadata_then_run_promise = TrySeq(
      stream->PushBatchToTransport(
          "recv_initial_metadata",
          [md = client_initial_metadata.get()](
              grpc_transport_stream_op_batch* batch, grpc_closure* on_done) {
            batch->recv_initial_metadata = true;
            batch->payload->recv_initial_metadata.recv_initial_metadata = md;
            batch->payload->recv_initial_metadata.recv_initial_metadata_ready =
                on_done;
          }),
      [client_initial_metadata = std::move(client_initial_metadata),
       next_promise_factory = std::move(next_promise_factory),
       call_data]() mutable {
        return Race(call_data->failure_latch.Wait(),
                    next_promise_factory(CallArgs{
                        std::move(client_initial_metadata),
                        ClientInitialMetadataOutstandingToken::Empty(),
                        &call_data->server_initial_metadata.sender,
                        &call_data->client_to_server.receiver,
                        &call_data->server_to_client.sender,
                    }));
      });
  auto run_request_then_send_trailing_metadata = Seq(
      std::move(recv_initial_metadata_then_run_promise),
      [call_data, stream = stream->InternalRef()](
          ServerMetadataHandle server_trailing_metadata) {
        return Map(
            stream->PushBatchToTransport(
                "send_trailing_metadata",
                [call_data, md = server_trailing_metadata.get()](
                    grpc_transport_stream_op_batch* batch,
                    grpc_closure* on_done) {
                  if (call_data->sent_initial_metadata) {
                    batch->send_trailing_metadata = true;
                    batch->payload->send_trailing_metadata.sent =
                        &call_data->sent_trailing_metadata;
                    batch->payload->send_trailing_metadata
                        .send_trailing_metadata = md;
                  } else {
                    call_data->sent_initial_metadata = true;
                    batch->cancel_stream = true;
                    const auto status_code = md->get(GrpcStatusMetadata())
                                                 .value_or(GRPC_STATUS_UNKNOWN);
                    batch->payload->cancel_stream.cancel_error =
                        grpc_error_set_int(
                            absl::Status(
                                static_cast<absl::StatusCode>(status_code),
                                md->GetOrCreatePointer(GrpcMessageMetadata())
                                    ->as_string_view()),
                            StatusIntProperty::kRpcStatus, status_code);
                  }
                  batch->on_complete = on_done;
                }),
            [call_data,
             server_trailing_metadata = std::move(server_trailing_metadata)](
                absl::Status status) mutable {
              if (!status.ok()) {
                server_trailing_metadata->Clear();
                server_trailing_metadata->Set(
                    GrpcStatusMetadata(),
                    static_cast<grpc_status_code>(status.code()));
                server_trailing_metadata->Set(
                    GrpcMessageMetadata(),
                    Slice::FromCopiedString(status.message()));
                server_trailing_metadata->Set(GrpcCallWasCancelled(), true);
              }
              if (!server_trailing_metadata->get(GrpcCallWasCancelled())
                       .has_value()) {
                server_trailing_metadata->Set(
                    GrpcCallWasCancelled(), !call_data->sent_trailing_metadata);
              }
              return std::move(server_trailing_metadata);
            });
      });

  auto recv_messages =
      Map(stream->RecvMessages(&call_data->client_to_server.sender),
          [failure_latch = &call_data->failure_latch](absl::Status status) {
            if (!status.ok() && !failure_latch->is_set()) {
              failure_latch->Set(ServerMetadataFromStatus(status));
            }
            return status;
          });

  auto trailing_metadata =
      GetContext<Arena>()->MakePooled<ClientMetadata>(GetContext<Arena>());
  auto* md = trailing_metadata.get();
  auto recv_trailing_metadata = Seq(
      stream->PushBatchToTransport(
          "recv_trailing_metadata",
          [md](grpc_transport_stream_op_batch* batch, grpc_closure* on_done) {
            batch->recv_trailing_metadata = true;
            batch->payload->recv_trailing_metadata.recv_trailing_metadata = md;
            batch->payload->recv_trailing_metadata.collect_stats =
                &GetContext<CallContext>()
                     ->call_stats()
                     ->transport_stream_stats;
            batch->payload->recv_trailing_metadata
                .recv_trailing_metadata_ready = on_done;
          }),
      [failure_latch = &call_data->failure_latch,
       trailing_metadata =
           std::move(trailing_metadata)](absl::Status status) mutable {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_DEBUG,
                  "%s[connected] Got trailing metadata; status=%s metadata=%s",
                  Activity::current()->DebugTag().c_str(),
                  status.ToString().c_str(),
                  trailing_metadata->DebugString().c_str());
        }
        if (!status.ok()) {
          trailing_metadata->Clear();
          grpc_status_code status_code = GRPC_STATUS_UNKNOWN;
          std::string message;
          grpc_error_get_status(status, Timestamp::InfFuture(), &status_code,
                                &message, nullptr, nullptr);
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
      });

  auto send_initial_metadata = Seq(
      call_data->server_initial_metadata.receiver.Next(),
      [call_data, stream = stream->InternalRef()](
          NextResult<ServerMetadataHandle> next_result) mutable {
        auto* md = !call_data->sent_initial_metadata && next_result.has_value()
                       ? next_result.value().get()
                       : nullptr;
        if (md != nullptr) call_data->sent_initial_metadata = true;
        return If(
            md != nullptr,
            [md, stream = std::move(stream),
             next_result = std::move(next_result)]() mutable {
              return Map(
                  stream->PushBatchToTransport(
                      "send_initial_metadata",
                      [md](grpc_transport_stream_op_batch* batch,
                           grpc_closure* on_done) {
                        GPR_ASSERT(md != nullptr);
                        batch->send_initial_metadata = true;
                        batch->payload->send_initial_metadata
                            .send_initial_metadata = md;
                        batch->payload->send_initial_metadata.peer_string =
                            nullptr;
                        batch->on_complete = on_done;
                      }),
                  [next_result = std::move(next_result)](absl::Status status) {
                    return status;
                  });
            },
            Immediate(absl::CancelledError()));
      });
  auto send_initial_metadata_then_messages =
      TrySeq(std::move(send_initial_metadata),
             stream->SendMessages(&call_data->server_to_client.receiver));

  party->Spawn("recv_messages", std::move(recv_messages), [](absl::Status) {});
  party->Spawn("send_initial_metadata_then_messages",
               std::move(send_initial_metadata_then_messages),
               [](absl::Status) {});
  party->Spawn("recv_trailing_metadata", std::move(recv_trailing_metadata),
               [](Empty) {});

  return Map(
      std::move(run_request_then_send_trailing_metadata),
      [stream = std::move(stream)](ServerMetadataHandle md) { return md; });
}
#endif

template <ArenaPromise<ServerMetadataHandle> (*make_call_promise)(
    grpc_transport*, CallArgs, NextPromiseFactory)>
grpc_channel_filter MakeConnectedFilter() {
  // Create a vtable that contains both the legacy call methods (for filter
  // stack based calls) and the new promise based method for creating
  // promise based calls (the latter iff make_call_promise != nullptr). In
  // this way the filter can be inserted into either kind of channel stack,
  // and only if all the filters in the stack are promise based will the
  // call be promise based.
  auto make_call_wrapper = +[](grpc_channel_element* elem, CallArgs call_args,
                               NextPromiseFactory next) {
    grpc_transport* transport =
        static_cast<channel_data*>(elem->channel_data)->transport;
    return make_call_promise(transport, std::move(call_args), std::move(next));
  };
  return {
      connected_channel_start_transport_stream_op_batch,
      make_call_promise != nullptr ? make_call_wrapper : nullptr,
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
        channel_stack->call_stack_size += grpc_transport_stream_size(
            static_cast<channel_data*>(elem->channel_data)->transport);
      },
      connected_channel_destroy_channel_elem,
      connected_channel_get_channel_info,
      "connected",
  };
}

ArenaPromise<ServerMetadataHandle> MakeTransportCallPromise(
    grpc_transport* transport, CallArgs call_args, NextPromiseFactory) {
  return transport->vtable->make_call_promise(transport, std::move(call_args));
}

const grpc_channel_filter kPromiseBasedTransportFilter =
    MakeConnectedFilter<MakeTransportCallPromise>();

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

}  // namespace
}  // namespace grpc_core

bool grpc_add_connected_filter(grpc_core::ChannelStackBuilder* builder) {
  grpc_transport* t = builder->transport();
  GPR_ASSERT(t != nullptr);
  // Choose the right vtable for the connected filter.
  // We can't know promise based call or not here (that decision needs the
  // collaboration of all of the filters on the channel, and we don't want
  // ordering constraints on when we add filters).
  // We can know if this results in a promise based call how we'll create
  // our promise (if indeed we can), and so that is the choice made here.
  if (t->vtable->make_call_promise != nullptr) {
    // Option 1, and our ideal: the transport supports promise based calls,
    // and so we simply use the transport directly.
    builder->AppendFilter(&grpc_core::kPromiseBasedTransportFilter);
  } else if (grpc_channel_stack_type_is_client(builder->channel_stack_type())) {
    // Option 2: the transport does not support promise based calls, but
    // we're on the client and so we have an implementation that we can use
    // to convert to batches.
    builder->AppendFilter(&grpc_core::kClientEmulatedFilter);
  } else {
    // Option 3: the transport does not support promise based calls, and
    // we're on the server so we use the server filter.
    builder->AppendFilter(&grpc_core::kServerEmulatedFilter);
  }
  return true;
}
