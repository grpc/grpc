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

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

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
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/detail/basic_seq.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/surface/channel_stack_type.h"
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
  grpc_transport* transport() { return transport_; }
  grpc_closure* stream_destroyed_closure() { return &stream_destroyed_; }

  void IncrementRefCount(const char* reason) {
#ifndef NDEBUG
    grpc_stream_ref(&stream_refcount_, reason);
#else
    (void)reason;
    grpc_stream_ref(&stream_refcount_);
#endif
  }

  void Unref(const char* reason) {
#ifndef NDEBUG
    grpc_stream_unref(&stream_refcount_, reason);
#else
    (void)reason;
    grpc_stream_unref(&stream_refcount_);
#endif
  }

  void Orphan() final {
    bool finished;
    {
      MutexLock lock(mu());
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO, "%s[connected] DropStream: %s finished=%s",
                Activity::current()->DebugTag().c_str(),
                ActiveOpsString().c_str(), finished_ ? "true" : "false");
      }
      finished = finished_;
    }
    // If we hadn't already observed the stream to be finished, we need to
    // cancel it at the transport.
    if (!finished) {
      IncrementRefCount("shutdown client stream");
      auto* cancel_op =
          GetContext<Arena>()->New<grpc_transport_stream_op_batch>();
      cancel_op->cancel_stream = true;
      cancel_op->payload = batch_payload();
      auto* s = stream();
      cancel_op->on_complete = NewClosure(
          [this](grpc_error_handle) { Unref("shutdown client stream"); });
      batch_payload()->cancel_stream.cancel_error = absl::CancelledError();
      grpc_transport_perform_stream_op(transport(), s, cancel_op);
    }
    Unref("orphan client stream");
  }

 protected:
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

  grpc_stream* stream() { return stream_.get(); }
  void SetStream(grpc_stream* stream) { stream_.reset(stream); }
  grpc_stream_refcount* stream_refcount() { return &stream_refcount_; }
  Mutex* mu() const ABSL_LOCK_RETURNED(mu_) { return &mu_; }
  grpc_transport_stream_op_batch_payload* batch_payload() {
    return &batch_payload_;
  }
  bool finished() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return finished_; }
  void set_finished() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { finished_ = true; }
  virtual std::string ActiveOpsString() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) = 0;

  void SchedulePush(grpc_transport_stream_op_batch* batch)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    batch->is_traced = GetContext<CallContext>()->traced();
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%s[connected] Push batch to transport: %s",
              Activity::current()->DebugTag().c_str(),
              grpc_transport_stream_op_batch_string(batch, false).c_str());
    }
    if (push_batches_.empty()) {
      IncrementRefCount("push");
      ExecCtx::Run(DEBUG_LOCATION, &push_, absl::OkStatus());
    }
    push_batches_.push_back(batch);
  }

  void PollSendMessage(PipeReceiver<MessageHandle>* outgoing_messages,
                       ClientMetadataHandle* client_trailing_metadata)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (absl::holds_alternative<Closed>(send_message_state_)) {
      message_to_send_.reset();
    }
    if (absl::holds_alternative<Idle>(send_message_state_)) {
      message_to_send_.reset();
      send_message_state_.emplace<PipeReceiverNextType<MessageHandle>>(
          outgoing_messages->Next());
    }
    if (auto* next = absl::get_if<PipeReceiverNextType<MessageHandle>>(
            &send_message_state_)) {
      auto r = (*next)();
      if (auto* p = r.value_if_ready()) {
        memset(&send_message_, 0, sizeof(send_message_));
        send_message_.payload = batch_payload();
        send_message_.on_complete = &send_message_batch_done_;
        // No value => half close from above.
        if (p->has_value()) {
          message_to_send_ = std::move(*p);
          send_message_state_ = SendMessageToTransport{};
          send_message_.send_message = true;
          batch_payload()->send_message.send_message =
              (*message_to_send_)->payload();
          batch_payload()->send_message.flags = (*message_to_send_)->flags();
        } else {
          if (grpc_call_trace.enabled()) {
            gpr_log(GPR_INFO, "%s[connected] PollConnectedChannel: half close",
                    Activity::current()->DebugTag().c_str());
          }
          GPR_ASSERT(!absl::holds_alternative<Closed>(send_message_state_));
          send_message_state_ = Closed{};
          send_message_.send_trailing_metadata = true;
          if (client_trailing_metadata != nullptr) {
            *client_trailing_metadata =
                GetContext<Arena>()->MakePooled<ClientMetadata>(
                    GetContext<Arena>());
            batch_payload()->send_trailing_metadata.send_trailing_metadata =
                client_trailing_metadata->get();
            batch_payload()->send_trailing_metadata.sent = nullptr;
          } else {
            return;  // Skip rest of function for server
          }
        }
        IncrementRefCount("send_message");
        send_message_waker_ = Activity::current()->MakeOwningWaker();
        SchedulePush(&send_message_);
      }
    }
  }

  void PollRecvMessage(PipeSender<MessageHandle>*& incoming_messages)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (auto* pending =
            absl::get_if<PendingReceiveMessage>(&recv_message_state_)) {
      if (pending->received) {
        if (pending->payload.has_value()) {
          if (grpc_call_trace.enabled()) {
            gpr_log(GPR_INFO,
                    "%s[connected] PollRecvMessage: received payload of "
                    "%" PRIdPTR " bytes",
                    recv_message_waker_.ActivityDebugTag().c_str(),
                    pending->payload->Length());
          }
          recv_message_state_ =
              incoming_messages->Push(GetContext<Arena>()->MakePooled<Message>(
                  std::move(*pending->payload), pending->flags));
        } else {
          if (grpc_call_trace.enabled()) {
            gpr_log(GPR_INFO,
                    "%s[connected] PollRecvMessage: received no payload",
                    recv_message_waker_.ActivityDebugTag().c_str());
          }
          recv_message_state_ = Closed{};
          std::exchange(incoming_messages, nullptr)->Close();
        }
      }
    }
    if (absl::holds_alternative<Idle>(recv_message_state_)) {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO, "%s[connected] PollRecvMessage: requesting message",
                Activity::current()->DebugTag().c_str());
      }
      PushRecvMessage();
    }
    if (auto* push = absl::get_if<PipeSender<MessageHandle>::PushType>(
            &recv_message_state_)) {
      auto r = (*push)();
      if (bool* result = r.value_if_ready()) {
        if (*result) {
          if (!finished_) {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%s[connected] PollRecvMessage: pushed message; "
                      "requesting next",
                      Activity::current()->DebugTag().c_str());
            }
            PushRecvMessage();
          } else {
            if (grpc_call_trace.enabled()) {
              gpr_log(GPR_INFO,
                      "%s[connected] PollRecvMessage: pushed message "
                      "and finished; "
                      "marking closed",
                      Activity::current()->DebugTag().c_str());
            }
            recv_message_state_ = Closed{};
            std::exchange(incoming_messages, nullptr)->Close();
          }
        } else {
          if (grpc_call_trace.enabled()) {
            gpr_log(GPR_INFO,
                    "%s[connected] PollRecvMessage: failed to push "
                    "message; marking "
                    "closed",
                    Activity::current()->DebugTag().c_str());
          }
          recv_message_state_ = Closed{};
          std::exchange(incoming_messages, nullptr)->Close();
        }
      }
    }
  }

  std::string SendMessageString() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return Match(
        send_message_state_, [](Idle) -> std::string { return "IDLE"; },
        [](Closed) -> std::string { return "CLOSED"; },
        [](const PipeReceiverNextType<MessageHandle>&) -> std::string {
          return "WAITING";
        },
        [](SendMessageToTransport) -> std::string { return "SENDING"; });
  }

  std::string RecvMessageString() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return Match(
        recv_message_state_, [](Idle) -> std::string { return "IDLE"; },
        [](Closed) -> std::string { return "CLOSED"; },
        [](const PendingReceiveMessage&) -> std::string { return "WAITING"; },
        [](const absl::optional<MessageHandle>& message) -> std::string {
          return absl::StrCat(
              "READY:", message.has_value()
                            ? absl::StrCat((*message)->payload()->Length(), "b")
                            : "EOS");
        },
        [](const PipeSender<MessageHandle>::PushType&) -> std::string {
          return "PUSHING";
        });
  }

  bool IsPromiseReceiving() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    return absl::holds_alternative<PipeSender<MessageHandle>::PushType>(
               recv_message_state_) ||
           absl::holds_alternative<PendingReceiveMessage>(recv_message_state_);
  }

 private:
  struct SendMessageToTransport {};
  struct Idle {};
  struct Closed {};

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

  // Called from outside the activity to push work down to the transport.
  void Push() {
    PushBatches push_batches;
    {
      MutexLock lock(&mu_);
      push_batches.swap(push_batches_);
    }
    for (auto* batch : push_batches) {
      if (stream() != nullptr) {
        grpc_transport_perform_stream_op(transport(), stream(), batch);
      } else {
        grpc_transport_stream_op_batch_finish_with_failure_from_transport(
            batch, absl::CancelledError());
      }
    }
    Unref("push");
  }

  void SendMessageBatchDone(grpc_error_handle error) {
    {
      MutexLock lock(&mu_);
      if (error != absl::OkStatus()) {
        // Note that we're in error here, the call will be closed by the
        // transport in a moment, and we'll return from the promise with an
        // error - so we don't need to do any extra work to close out pipes or
        // the like.
        send_message_state_ = Closed{};
      }
      if (!absl::holds_alternative<Closed>(send_message_state_)) {
        send_message_state_ = Idle{};
      }
      send_message_waker_.Wakeup();
    }
    Unref("send_message");
  }

  void RecvMessageBatchDone(grpc_error_handle error) {
    {
      MutexLock lock(mu());
      if (error != absl::OkStatus()) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%s[connected] RecvMessageBatchDone: error=%s",
                  recv_message_waker_.ActivityDebugTag().c_str(),
                  StatusToString(error).c_str());
        }
      } else if (absl::holds_alternative<Closed>(recv_message_state_)) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "%s[connected] RecvMessageBatchDone: already closed, "
                  "ignoring",
                  recv_message_waker_.ActivityDebugTag().c_str());
        }
      } else {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO,
                  "%s[connected] RecvMessageBatchDone: received message",
                  recv_message_waker_.ActivityDebugTag().c_str());
        }
        auto pending =
            absl::get_if<PendingReceiveMessage>(&recv_message_state_);
        GPR_ASSERT(pending != nullptr);
        GPR_ASSERT(pending->received == false);
        pending->received = true;
      }
      recv_message_waker_.Wakeup();
    }
    Unref("recv_message");
  }

  void PushRecvMessage() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    recv_message_state_ = PendingReceiveMessage{};
    auto& pending_recv_message =
        absl::get<PendingReceiveMessage>(recv_message_state_);
    memset(&recv_message_, 0, sizeof(recv_message_));
    recv_message_.payload = batch_payload();
    recv_message_.on_complete = nullptr;
    recv_message_.recv_message = true;
    batch_payload()->recv_message.recv_message = &pending_recv_message.payload;
    batch_payload()->recv_message.flags = &pending_recv_message.flags;
    batch_payload()->recv_message.call_failed_before_recv_message = nullptr;
    batch_payload()->recv_message.recv_message_ready =
        &recv_message_batch_done_;
    IncrementRefCount("recv_message");
    recv_message_waker_ = Activity::current()->MakeOwningWaker();
    SchedulePush(&recv_message_);
  }

  mutable Mutex mu_;
  grpc_transport* const transport_;
  CallContext* const call_context_{GetContext<CallContext>()};
  grpc_closure stream_destroyed_ =
      MakeMemberClosure<ConnectedChannelStream,
                        &ConnectedChannelStream::StreamDestroyed>(
          this, DEBUG_LOCATION);
  grpc_stream_refcount stream_refcount_;
  StreamPtr stream_;
  using PushBatches = absl::InlinedVector<grpc_transport_stream_op_batch*, 3>;
  PushBatches push_batches_ ABSL_GUARDED_BY(mu_);
  grpc_closure push_ =
      MakeMemberClosure<ConnectedChannelStream, &ConnectedChannelStream::Push>(
          this, DEBUG_LOCATION);

  NextResult<MessageHandle> message_to_send_ ABSL_GUARDED_BY(mu_);
  absl::variant<Idle, Closed, PipeReceiverNextType<MessageHandle>,
                SendMessageToTransport>
      send_message_state_ ABSL_GUARDED_BY(mu_);
  grpc_transport_stream_op_batch send_message_;
  grpc_closure send_message_batch_done_ =
      MakeMemberClosure<ConnectedChannelStream,
                        &ConnectedChannelStream::SendMessageBatchDone>(
          this, DEBUG_LOCATION);

  struct PendingReceiveMessage {
    absl::optional<SliceBuffer> payload;
    uint32_t flags;
    bool received = false;
  };
  absl::variant<Idle, PendingReceiveMessage, Closed,
                PipeSender<MessageHandle>::PushType>
      recv_message_state_ ABSL_GUARDED_BY(mu_);
  grpc_closure recv_message_batch_done_ =
      MakeMemberClosure<ConnectedChannelStream,
                        &ConnectedChannelStream::RecvMessageBatchDone>(
          this, DEBUG_LOCATION);
  grpc_transport_stream_op_batch recv_message_;

  Waker send_message_waker_ ABSL_GUARDED_BY(mu_);
  Waker recv_message_waker_ ABSL_GUARDED_BY(mu_);
  bool finished_ ABSL_GUARDED_BY(mu_) = false;

  grpc_transport_stream_op_batch_payload batch_payload_{
      GetContext<grpc_call_context_element>()};
};
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_CLIENT_CALL
class ClientStream : public ConnectedChannelStream {
 public:
  ClientStream(grpc_transport* transport, CallArgs call_args)
      : ConnectedChannelStream(transport),
        server_initial_metadata_pipe_(call_args.server_initial_metadata),
        client_to_server_messages_(call_args.client_to_server_messages),
        server_to_client_messages_(call_args.server_to_client_messages),
        client_initial_metadata_(std::move(call_args.client_initial_metadata)) {
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO, "%s[connected] InitImpl: intitial_metadata=%s",
              Activity::current()->DebugTag().c_str(),
              client_initial_metadata_->DebugString().c_str());
    }
  }

  Poll<ServerMetadataHandle> PollOnce() {
    MutexLock lock(mu());
    GPR_ASSERT(!finished());

    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO, "%s[connected] PollConnectedChannel: %s",
              Activity::current()->DebugTag().c_str(),
              ActiveOpsString().c_str());
    }

    if (!std::exchange(requested_metadata_, true)) {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "%s[connected] PollConnectedChannel: requesting metadata",
                Activity::current()->DebugTag().c_str());
      }
      SetStream(static_cast<grpc_stream*>(
          GetContext<Arena>()->Alloc(transport()->vtable->sizeof_stream)));
      grpc_transport_init_stream(transport(), stream(), stream_refcount(),
                                 nullptr, GetContext<Arena>());
      grpc_transport_set_pops(transport(), stream(),
                              GetContext<CallContext>()->polling_entity());
      memset(&metadata_, 0, sizeof(metadata_));
      metadata_.send_initial_metadata = true;
      metadata_.recv_initial_metadata = true;
      metadata_.recv_trailing_metadata = true;
      metadata_.payload = batch_payload();
      metadata_.on_complete = &metadata_batch_done_;
      batch_payload()->send_initial_metadata.send_initial_metadata =
          client_initial_metadata_.get();
      server_initial_metadata_ =
          GetContext<Arena>()->MakePooled<ServerMetadata>(GetContext<Arena>());
      batch_payload()->recv_initial_metadata.recv_initial_metadata =
          server_initial_metadata_.get();
      batch_payload()->recv_initial_metadata.recv_initial_metadata_ready =
          &recv_initial_metadata_ready_;
      batch_payload()->recv_initial_metadata.trailing_metadata_available =
          nullptr;
      server_trailing_metadata_ =
          GetContext<Arena>()->MakePooled<ServerMetadata>(GetContext<Arena>());
      batch_payload()->recv_trailing_metadata.recv_trailing_metadata =
          server_trailing_metadata_.get();
      batch_payload()->recv_trailing_metadata.collect_stats =
          &GetContext<CallContext>()->call_stats()->transport_stream_stats;
      batch_payload()->recv_trailing_metadata.recv_trailing_metadata_ready =
          &recv_trailing_metadata_ready_;
      IncrementRefCount("metadata_batch_done");
      IncrementRefCount("initial_metadata_ready");
      IncrementRefCount("trailing_metadata_ready");
      initial_metadata_waker_ = Activity::current()->MakeOwningWaker();
      trailing_metadata_waker_ = Activity::current()->MakeOwningWaker();
      SchedulePush(&metadata_);
    }
    if (server_initial_metadata_state_ ==
        ServerInitialMetadataState::kReceivedButNotPushed) {
      server_initial_metadata_state_ = ServerInitialMetadataState::kPushing;
      server_initial_metadata_push_promise_ =
          server_initial_metadata_pipe_->Push(
              std::move(server_initial_metadata_));
    }
    if (server_initial_metadata_state_ ==
        ServerInitialMetadataState::kPushing) {
      auto r = (*server_initial_metadata_push_promise_)();
      if (r.ready()) {
        server_initial_metadata_state_ = ServerInitialMetadataState::kPushed;
        server_initial_metadata_push_promise_.reset();
      }
    }
    PollSendMessage(client_to_server_messages_, &client_trailing_metadata_);
    PollRecvMessage(server_to_client_messages_);
    if (server_initial_metadata_state_ == ServerInitialMetadataState::kPushed &&
        !IsPromiseReceiving() &&
        std::exchange(queued_trailing_metadata_, false)) {
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_INFO,
                "%s[connected] PollConnectedChannel: finished request, "
                "returning: {%s}; "
                "active_ops: %s",
                Activity::current()->DebugTag().c_str(),
                server_trailing_metadata_->DebugString().c_str(),
                ActiveOpsString().c_str());
      }
      set_finished();
      return ServerMetadataHandle(std::move(server_trailing_metadata_));
    }
    return Pending{};
  }

  void RecvInitialMetadataReady(grpc_error_handle error) {
    GPR_ASSERT(error == absl::OkStatus());
    {
      MutexLock lock(mu());
      server_initial_metadata_state_ =
          ServerInitialMetadataState::kReceivedButNotPushed;
      initial_metadata_waker_.Wakeup();
    }
    Unref("initial_metadata_ready");
  }

  void RecvTrailingMetadataReady(grpc_error_handle error) {
    GPR_ASSERT(error == absl::OkStatus());
    {
      MutexLock lock(mu());
      queued_trailing_metadata_ = true;
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_DEBUG,
                "%s[connected] RecvTrailingMetadataReady: "
                "queued_trailing_metadata_ "
                "set to true; active_ops: %s",
                trailing_metadata_waker_.ActivityDebugTag().c_str(),
                ActiveOpsString().c_str());
      }
      trailing_metadata_waker_.Wakeup();
    }
    Unref("trailing_metadata_ready");
  }

  void MetadataBatchDone(grpc_error_handle error) {
    GPR_ASSERT(error == absl::OkStatus());
    Unref("metadata_batch_done");
  }

 private:
  enum class ServerInitialMetadataState : uint8_t {
    // Initial metadata has not been received from the server.
    kNotReceived,
    // Initial metadata has been received from the server via the transport, but
    // has not yet been pushed onto the pipe to publish it up the call stack.
    kReceivedButNotPushed,
    // Initial metadata has been received from the server via the transport and
    // has been pushed on the pipe to publish it up the call stack.
    // It's still in the pipe and has not been removed by the call at the top
    // yet.
    kPushing,
    // Initial metadata has been received from the server via the transport and
    // has been pushed on the pipe to publish it up the call stack AND removed
    // by the call at the top.
    kPushed,
  };

  std::string ActiveOpsString() const override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    std::vector<std::string> ops;
    if (finished()) ops.push_back("FINISHED");
    // Outstanding Operations on Transport
    std::vector<std::string> waiting;
    if (initial_metadata_waker_ != Waker()) {
      waiting.push_back("initial_metadata");
    }
    if (trailing_metadata_waker_ != Waker()) {
      waiting.push_back("trailing_metadata");
    }
    if (!waiting.empty()) {
      ops.push_back(absl::StrCat("waiting:", absl::StrJoin(waiting, ",")));
    }
    // Results from transport
    std::vector<std::string> queued;
    if (server_initial_metadata_state_ ==
        ServerInitialMetadataState::kReceivedButNotPushed) {
      queued.push_back("initial_metadata");
    }
    if (queued_trailing_metadata_) queued.push_back("trailing_metadata");
    if (!queued.empty()) {
      ops.push_back(absl::StrCat("queued:", absl::StrJoin(queued, ",")));
    }
    // Send message
    std::string send_message_state = SendMessageString();
    if (send_message_state != "WAITING") {
      ops.push_back(absl::StrCat("send_message:", send_message_state));
    }
    // Receive message
    std::string recv_message_state = RecvMessageString();
    if (recv_message_state != "IDLE") {
      ops.push_back(absl::StrCat("recv_message:", recv_message_state));
    }
    return absl::StrJoin(ops, " ");
  }

  bool requested_metadata_ = false;
  ServerInitialMetadataState server_initial_metadata_state_
      ABSL_GUARDED_BY(mu()) = ServerInitialMetadataState::kNotReceived;
  bool queued_trailing_metadata_ ABSL_GUARDED_BY(mu()) = false;
  Waker initial_metadata_waker_ ABSL_GUARDED_BY(mu());
  Waker trailing_metadata_waker_ ABSL_GUARDED_BY(mu());
  PipeSender<ServerMetadataHandle>* server_initial_metadata_pipe_;
  PipeReceiver<MessageHandle>* client_to_server_messages_;
  PipeSender<MessageHandle>* server_to_client_messages_;
  grpc_closure recv_initial_metadata_ready_ =
      MakeMemberClosure<ClientStream, &ClientStream::RecvInitialMetadataReady>(
          this, DEBUG_LOCATION);
  grpc_closure recv_trailing_metadata_ready_ =
      MakeMemberClosure<ClientStream, &ClientStream::RecvTrailingMetadataReady>(
          this, DEBUG_LOCATION);
  ClientMetadataHandle client_initial_metadata_;
  ClientMetadataHandle client_trailing_metadata_;
  ServerMetadataHandle server_initial_metadata_;
  ServerMetadataHandle server_trailing_metadata_;
  absl::optional<PipeSender<ServerMetadataHandle>::PushType>
      server_initial_metadata_push_promise_;
  grpc_transport_stream_op_batch metadata_;
  grpc_closure metadata_batch_done_ =
      MakeMemberClosure<ClientStream, &ClientStream::MetadataBatchDone>(
          this, DEBUG_LOCATION);
};

class ClientConnectedCallPromise {
 public:
  ClientConnectedCallPromise(grpc_transport* transport, CallArgs call_args)
      : impl_(GetContext<Arena>()->New<ClientStream>(transport,
                                                     std::move(call_args))) {}

  ClientConnectedCallPromise(const ClientConnectedCallPromise&) = delete;
  ClientConnectedCallPromise& operator=(const ClientConnectedCallPromise&) =
      delete;
  ClientConnectedCallPromise(ClientConnectedCallPromise&& other) noexcept
      : impl_(std::exchange(other.impl_, nullptr)) {}
  ClientConnectedCallPromise& operator=(
      ClientConnectedCallPromise&& other) noexcept {
    impl_ = std::move(other.impl_);
    return *this;
  }

  static ArenaPromise<ServerMetadataHandle> Make(grpc_transport* transport,
                                                 CallArgs call_args,
                                                 NextPromiseFactory) {
    return ClientConnectedCallPromise(transport, std::move(call_args));
  }

  Poll<ServerMetadataHandle> operator()() { return impl_->PollOnce(); }

 private:
  OrphanablePtr<ClientStream> impl_;
};
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL
class ServerStream final : public ConnectedChannelStream {
 public:
  ServerStream(grpc_transport* transport,
               NextPromiseFactory next_promise_factory)
      : ConnectedChannelStream(transport) {
    SetStream(static_cast<grpc_stream*>(
        GetContext<Arena>()->Alloc(transport->vtable->sizeof_stream)));
    grpc_transport_init_stream(
        transport, stream(), stream_refcount(),
        GetContext<CallContext>()->server_call_context()->server_stream_data(),
        GetContext<Arena>());
    grpc_transport_set_pops(transport, stream(),
                            GetContext<CallContext>()->polling_entity());

    // Fetch initial metadata
    auto& gim = call_state_.emplace<GettingInitialMetadata>(this);
    gim.recv_initial_metadata_ready_waker =
        Activity::current()->MakeOwningWaker();
    memset(&gim.recv_initial_metadata, 0, sizeof(gim.recv_initial_metadata));
    gim.recv_initial_metadata.payload = batch_payload();
    gim.recv_initial_metadata.on_complete = nullptr;
    gim.recv_initial_metadata.recv_initial_metadata = true;
    gim.next_promise_factory = std::move(next_promise_factory);
    batch_payload()->recv_initial_metadata.recv_initial_metadata =
        gim.client_initial_metadata.get();
    batch_payload()->recv_initial_metadata.recv_initial_metadata_ready =
        &gim.recv_initial_metadata_ready;
    SchedulePush(&gim.recv_initial_metadata);

    // Fetch trailing metadata (to catch cancellations)
    auto& gtm =
        client_trailing_metadata_state_.emplace<WaitingForTrailingMetadata>();
    gtm.recv_trailing_metadata_ready =
        MakeMemberClosure<ServerStream,
                          &ServerStream::RecvTrailingMetadataReady>(this);
    memset(&gtm.recv_trailing_metadata, 0, sizeof(gtm.recv_trailing_metadata));
    gtm.recv_trailing_metadata.payload = batch_payload();
    gtm.recv_trailing_metadata.recv_trailing_metadata = true;
    batch_payload()->recv_trailing_metadata.recv_trailing_metadata =
        gtm.result.get();
    batch_payload()->recv_trailing_metadata.collect_stats =
        &GetContext<CallContext>()->call_stats()->transport_stream_stats;
    batch_payload()->recv_trailing_metadata.recv_trailing_metadata_ready =
        &gtm.recv_trailing_metadata_ready;
    SchedulePush(&gtm.recv_trailing_metadata);
    gtm.waker = Activity::current()->MakeOwningWaker();
  }

  Poll<ServerMetadataHandle> PollOnce() {
    MutexLock lock(mu());

    auto poll_send_initial_metadata = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(
                                          mu()) {
      if (auto* promise =
              absl::get_if<PipeReceiverNextType<ServerMetadataHandle>>(
                  &server_initial_metadata_)) {
        auto r = (*promise)();
        if (auto* md = r.value_if_ready()) {
          if (grpc_call_trace.enabled()) {
            gpr_log(
                GPR_INFO, "%s[connected] got initial metadata %s",
                Activity::current()->DebugTag().c_str(),
                (md->has_value() ? (**md)->DebugString() : "<trailers-only>")
                    .c_str());
          }
          memset(&send_initial_metadata_, 0, sizeof(send_initial_metadata_));
          send_initial_metadata_.send_initial_metadata = true;
          send_initial_metadata_.payload = batch_payload();
          send_initial_metadata_.on_complete = &send_initial_metadata_done_;
          batch_payload()->send_initial_metadata.send_initial_metadata =
              server_initial_metadata_
                  .emplace<ServerMetadataHandle>(std::move(**md))
                  .get();
          SchedulePush(&send_initial_metadata_);
          return true;
        } else {
          return false;
        }
      } else {
        return true;
      }
    };

    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO, "%s[connected] PollConnectedChannel: %s",
              Activity::current()->DebugTag().c_str(),
              ActiveOpsString().c_str());
    }

    poll_send_initial_metadata();

    if (auto* p = absl::get_if<GotClientHalfClose>(
            &client_trailing_metadata_state_)) {
      pipes_.client_to_server.sender.Close();
      if (!p->result.ok()) {
        // client cancelled, we should cancel too
        if (absl::holds_alternative<absl::monostate>(call_state_) ||
            absl::holds_alternative<GotInitialMetadata>(call_state_) ||
            absl::holds_alternative<MessageLoop>(call_state_)) {
          if (!absl::holds_alternative<ServerMetadataHandle>(
                  server_initial_metadata_)) {
            // pretend we've sent initial metadata to stop that op from
            // progressing if it's stuck somewhere above us in the stack
            server_initial_metadata_.emplace<ServerMetadataHandle>();
          }
          // cancel the call - this status will be returned to the server bottom
          // promise
          call_state_.emplace<Complete>(
              Complete{ServerMetadataFromStatus(p->result)});
        }
      }
    }

    if (auto* p = absl::get_if<GotInitialMetadata>(&call_state_)) {
      incoming_messages_ = &pipes_.client_to_server.sender;
      auto promise = p->next_promise_factory(CallArgs{
          std::move(p->client_initial_metadata),
          &pipes_.server_initial_metadata.sender,
          &pipes_.client_to_server.receiver, &pipes_.server_to_client.sender});
      call_state_.emplace<MessageLoop>(
          MessageLoop{&pipes_.server_to_client.receiver, std::move(promise)});
      server_initial_metadata_
          .emplace<PipeReceiverNextType<ServerMetadataHandle>>(
              pipes_.server_initial_metadata.receiver.Next());
    }
    if (incoming_messages_ != nullptr) {
      PollRecvMessage(incoming_messages_);
    }
    if (auto* p = absl::get_if<MessageLoop>(&call_state_)) {
      if (absl::holds_alternative<ServerMetadataHandle>(
              server_initial_metadata_)) {
        PollSendMessage(p->outgoing_messages, nullptr);
      }
      auto poll = p->promise();
      if (auto* r = poll.value_if_ready()) {
        if (grpc_call_trace.enabled()) {
          gpr_log(GPR_INFO, "%s[connected] got trailing metadata %s; %s",
                  Activity::current()->DebugTag().c_str(),
                  (*r)->DebugString().c_str(), ActiveOpsString().c_str());
        }
        auto& completing = call_state_.emplace<Completing>();
        completing.server_trailing_metadata = std::move(*r);
        completing.on_complete =
            MakeMemberClosure<ServerStream,
                              &ServerStream::SendTrailingMetadataDone>(this);
        completing.waker = Activity::current()->MakeOwningWaker();
        auto& op = completing.send_trailing_metadata;
        memset(&op, 0, sizeof(op));
        op.payload = batch_payload();
        op.on_complete = &completing.on_complete;
        // If we've gotten initial server metadata, we can send trailing
        // metadata.
        // Otherwise we need to cancel the call.
        // There could be an unlucky ordering, so we poll here to make sure.
        if (poll_send_initial_metadata()) {
          op.send_trailing_metadata = true;
          batch_payload()->send_trailing_metadata.send_trailing_metadata =
              completing.server_trailing_metadata.get();
          batch_payload()->send_trailing_metadata.sent = &completing.sent;
        } else {
          op.cancel_stream = true;
          const auto status_code =
              completing.server_trailing_metadata->get(GrpcStatusMetadata())
                  .value_or(GRPC_STATUS_UNKNOWN);
          batch_payload()->cancel_stream.cancel_error = grpc_error_set_int(
              absl::Status(static_cast<absl::StatusCode>(status_code),
                           completing.server_trailing_metadata
                               ->GetOrCreatePointer(GrpcMessageMetadata())
                               ->as_string_view()),
              StatusIntProperty::kRpcStatus, status_code);
        }
        SchedulePush(&op);
      }
    }
    if (auto* p = absl::get_if<Complete>(&call_state_)) {
      set_finished();
      return std::move(p->result);
    }
    return Pending{};
  }

 private:
  // Call state: we've asked the transport for initial metadata and are
  // waiting for it before proceeding.
  struct GettingInitialMetadata {
    explicit GettingInitialMetadata(ServerStream* stream)
        : recv_initial_metadata_ready(
              MakeMemberClosure<ServerStream,
                                &ServerStream::RecvInitialMetadataReady>(
                  stream)) {}
    // The batch we're using to get initial metadata.
    grpc_transport_stream_op_batch recv_initial_metadata;
    // Waker to re-enter the activity once the transport returns.
    Waker recv_initial_metadata_ready_waker;
    // Initial metadata storage for the transport.
    ClientMetadataHandle client_initial_metadata =
        GetContext<Arena>()->MakePooled<ClientMetadata>(GetContext<Arena>());
    // Closure for the transport to call when it's ready.
    grpc_closure recv_initial_metadata_ready;
    // Next promise factory to use once we have initial metadata.
    NextPromiseFactory next_promise_factory;
  };

  // Call state: transport has returned initial metadata, we're waiting to
  // re-enter the activity to process it.
  struct GotInitialMetadata {
    ClientMetadataHandle client_initial_metadata;
    NextPromiseFactory next_promise_factory;
  };

  // Call state: we're sending/receiving messages and processing the filter
  // stack.
  struct MessageLoop {
    PipeReceiver<MessageHandle>* outgoing_messages;
    ArenaPromise<ServerMetadataHandle> promise;
  };

  // Call state: promise stack has returned trailing metadata, we're sending it
  // to the transport to communicate.
  struct Completing {
    ServerMetadataHandle server_trailing_metadata;
    grpc_transport_stream_op_batch send_trailing_metadata;
    grpc_closure on_complete;
    bool sent = false;
    Waker waker;
  };

  // Call state: server metadata has been communicated to the transport and sent
  // to the client.
  // The metadata will be returned down to the server call to tick the
  // cancellation bit or not on the originating batch.
  struct Complete {
    ServerMetadataHandle result;
  };

  // Trailing metadata state: we've asked the transport for trailing metadata
  // and are waiting for it before proceeding.
  struct WaitingForTrailingMetadata {
    ClientMetadataHandle result =
        GetContext<Arena>()->MakePooled<ClientMetadata>(GetContext<Arena>());
    grpc_transport_stream_op_batch recv_trailing_metadata;
    grpc_closure recv_trailing_metadata_ready;
    Waker waker;
  };

  // We've received trailing metadata from the transport - which indicates reads
  // are closed.
  // We convert to an absl::Status here and use that to drive a decision to
  // cancel the call (on error) or not.
  struct GotClientHalfClose {
    absl::Status result;
  };

  void RecvInitialMetadataReady(absl::Status status) {
    MutexLock lock(mu());
    auto& getting = absl::get<GettingInitialMetadata>(call_state_);
    auto waker = std::move(getting.recv_initial_metadata_ready_waker);
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%sGOT INITIAL METADATA: err=%s %s",
              waker.ActivityDebugTag().c_str(), status.ToString().c_str(),
              getting.client_initial_metadata->DebugString().c_str());
    }
    GotInitialMetadata got{std::move(getting.client_initial_metadata),
                           std::move(getting.next_promise_factory)};
    call_state_.emplace<GotInitialMetadata>(std::move(got));
    waker.Wakeup();
  }

  void SendTrailingMetadataDone(absl::Status result) {
    MutexLock lock(mu());
    auto& completing = absl::get<Completing>(call_state_);
    auto md = std::move(completing.server_trailing_metadata);
    auto waker = std::move(completing.waker);
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG, "%sSEND TRAILING METADATA DONE: err=%s sent=%s %s",
              waker.ActivityDebugTag().c_str(), result.ToString().c_str(),
              completing.sent ? "true" : "false", md->DebugString().c_str());
    }
    md->Set(GrpcStatusFromWire(), completing.sent);
    if (!result.ok()) {
      md->Clear();
      md->Set(GrpcStatusMetadata(),
              static_cast<grpc_status_code>(result.code()));
      md->Set(GrpcMessageMetadata(), Slice::FromCopiedString(result.message()));
      md->Set(GrpcStatusFromWire(), false);
    }
    call_state_.emplace<Complete>(Complete{std::move(md)});
    waker.Wakeup();
  }

  std::string ActiveOpsString() const override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu()) {
    std::vector<std::string> ops;
    ops.push_back(absl::StrCat(
        "call_state:",
        Match(
            call_state_,
            [](const absl::monostate&) { return "absl::monostate"; },
            [](const GettingInitialMetadata&) { return "GETTING"; },
            [](const GotInitialMetadata&) { return "GOT"; },
            [](const MessageLoop&) { return "RUNNING"; },
            [](const Completing&) { return "COMPLETING"; },
            [](const Complete&) { return "COMPLETE"; })));
    ops.push_back(
        absl::StrCat("client_trailing_metadata_state:",
                     Match(
                         client_trailing_metadata_state_,
                         [](const absl::monostate&) -> std::string {
                           return "absl::monostate";
                         },
                         [](const WaitingForTrailingMetadata&) -> std::string {
                           return "WAITING";
                         },
                         [](const GotClientHalfClose& got) -> std::string {
                           return absl::StrCat("GOT:", got.result.ToString());
                         })));
    // Send initial metadata
    ops.push_back(absl::StrCat(
        "server_initial_metadata_state:",
        Match(
            server_initial_metadata_,
            [](const absl::monostate&) { return "absl::monostate"; },
            [](const PipeReceiverNextType<ServerMetadataHandle>&) {
              return "WAITING";
            },
            [](const ServerMetadataHandle&) { return "GOT"; })));
    // Send message
    std::string send_message_state = SendMessageString();
    if (send_message_state != "WAITING") {
      ops.push_back(absl::StrCat("send_message:", send_message_state));
    }
    // Receive message
    std::string recv_message_state = RecvMessageString();
    if (recv_message_state != "IDLE") {
      ops.push_back(absl::StrCat("recv_message:", recv_message_state));
    }
    return absl::StrJoin(ops, " ");
  }

  void SendInitialMetadataDone() {}

  void RecvTrailingMetadataReady(absl::Status error) {
    MutexLock lock(mu());
    auto& state =
        absl::get<WaitingForTrailingMetadata>(client_trailing_metadata_state_);
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_INFO,
              "%sRecvTrailingMetadataReady: error:%s metadata:%s state:%s",
              state.waker.ActivityDebugTag().c_str(), error.ToString().c_str(),
              state.result->DebugString().c_str(), ActiveOpsString().c_str());
    }
    auto waker = std::move(state.waker);
    ServerMetadataHandle result = std::move(state.result);
    if (error.ok()) {
      auto* message = result->get_pointer(GrpcMessageMetadata());
      error = absl::Status(
          static_cast<absl::StatusCode>(
              result->get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN)),
          message == nullptr ? "" : message->as_string_view());
    }
    client_trailing_metadata_state_.emplace<GotClientHalfClose>(
        GotClientHalfClose{error});
    waker.Wakeup();
  }

  struct Pipes {
    Pipe<MessageHandle> server_to_client;
    Pipe<MessageHandle> client_to_server;
    Pipe<ServerMetadataHandle> server_initial_metadata;
  };

  using CallState =
      absl::variant<absl::monostate, GettingInitialMetadata, GotInitialMetadata,
                    MessageLoop, Completing, Complete>;
  CallState call_state_ ABSL_GUARDED_BY(mu()) = absl::monostate{};
  using ClientTrailingMetadataState =
      absl::variant<absl::monostate, WaitingForTrailingMetadata,
                    GotClientHalfClose>;
  ClientTrailingMetadataState client_trailing_metadata_state_
      ABSL_GUARDED_BY(mu()) = absl::monostate{};
  absl::variant<absl::monostate, PipeReceiverNextType<ServerMetadataHandle>,
                ServerMetadataHandle>
      ABSL_GUARDED_BY(mu()) server_initial_metadata_ = absl::monostate{};
  PipeSender<MessageHandle>* incoming_messages_ = nullptr;
  grpc_transport_stream_op_batch send_initial_metadata_;
  grpc_closure send_initial_metadata_done_ =
      MakeMemberClosure<ServerStream, &ServerStream::SendInitialMetadataDone>(
          this);
  Pipes pipes_ ABSL_GUARDED_BY(mu());
};

class ServerConnectedCallPromise {
 public:
  ServerConnectedCallPromise(grpc_transport* transport,
                             NextPromiseFactory next_promise_factory)
      : impl_(GetContext<Arena>()->New<ServerStream>(
            transport, std::move(next_promise_factory))) {}

  ServerConnectedCallPromise(const ServerConnectedCallPromise&) = delete;
  ServerConnectedCallPromise& operator=(const ServerConnectedCallPromise&) =
      delete;
  ServerConnectedCallPromise(ServerConnectedCallPromise&& other) noexcept
      : impl_(std::exchange(other.impl_, nullptr)) {}
  ServerConnectedCallPromise& operator=(
      ServerConnectedCallPromise&& other) noexcept {
    impl_ = std::move(other.impl_);
    return *this;
  }

  static ArenaPromise<ServerMetadataHandle> Make(grpc_transport* transport,
                                                 CallArgs,
                                                 NextPromiseFactory next) {
    return ServerConnectedCallPromise(transport, std::move(next));
  }

  Poll<ServerMetadataHandle> operator()() { return impl_->PollOnce(); }

 private:
  OrphanablePtr<ServerStream> impl_;
};
#endif

template <ArenaPromise<ServerMetadataHandle> (*make_call_promise)(
    grpc_transport*, CallArgs, NextPromiseFactory)>
grpc_channel_filter MakeConnectedFilter() {
  // Create a vtable that contains both the legacy call methods (for filter
  // stack based calls) and the new promise based method for creating promise
  // based calls (the latter iff make_call_promise != nullptr).
  // In this way the filter can be inserted into either kind of channel stack,
  // and only if all the filters in the stack are promise based will the call
  // be promise based.
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
        // HACK(ctiller): increase call stack size for the channel to make space
        // for channel data. We need a cleaner (but performant) way to do this,
        // and I'm not sure what that is yet.
        // This is only "safe" because call stacks place no additional data
        // after the last call element, and the last call element MUST be the
        // connected channel.
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
    MakeConnectedFilter<ClientConnectedCallPromise::Make>();
#else
const grpc_channel_filter kClientEmulatedFilter =
    MakeConnectedFilter<nullptr>();
#endif

#ifdef GRPC_EXPERIMENT_IS_INCLUDED_PROMISE_BASED_SERVER_CALL
const grpc_channel_filter kServerEmulatedFilter =
    MakeConnectedFilter<ServerConnectedCallPromise::Make>();
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
  // We can know if this results in a promise based call how we'll create our
  // promise (if indeed we can), and so that is the choice made here.
  if (t->vtable->make_call_promise != nullptr) {
    // Option 1, and our ideal: the transport supports promise based calls, and
    // so we simply use the transport directly.
    builder->AppendFilter(&grpc_core::kPromiseBasedTransportFilter);
  } else if (grpc_channel_stack_type_is_client(builder->channel_stack_type())) {
    // Option 2: the transport does not support promise based calls, but we're
    // on the client and so we have an implementation that we can use to convert
    // to batches.
    builder->AppendFilter(&grpc_core::kClientEmulatedFilter);
  } else {
    // Option 3: the transport does not support promise based calls, and we're
    // on the server so we use the server filter.
    builder->AppendFilter(&grpc_core::kServerEmulatedFilter);
  }
  return true;
}
