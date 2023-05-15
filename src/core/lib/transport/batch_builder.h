// Copyright 2023 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_BATCH_BUILDER_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_BATCH_BUILDER_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/surface/call_trace.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_fwd.h"

namespace grpc_core {

// Build up a transport stream op batch for a stream for a promise based
// connected channel.
// Offered as a context from Call, so that it can collect ALL the updates during
// a single party round, and then push them down to the transport as a single
// transaction.
class BatchBuilder {
 public:
  explicit BatchBuilder(grpc_transport_stream_op_batch_payload* payload);
  ~BatchBuilder() {
    if (batch_ != nullptr) FlushBatch();
  }

  struct Target {
    grpc_transport* transport;
    grpc_stream* stream;
    grpc_stream_refcount* stream_refcount;
  };

  BatchBuilder(const BatchBuilder&) = delete;
  BatchBuilder& operator=(const BatchBuilder&) = delete;

  // Returns a promise that will resolve to a Status when the send is completed.
  auto SendMessage(Target target, MessageHandle message);

  // Returns a promise that will resolve to a Status when the send is completed.
  auto SendClientInitialMetadata(Target target, ClientMetadataHandle metadata);

  // Returns a promise that will resolve to a Status when the send is completed.
  auto SendClientTrailingMetadata(Target target);

  // Returns a promise that will resolve to a Status when the send is completed.
  auto SendServerInitialMetadata(Target target, ServerMetadataHandle metadata);

  // Returns a promise that will resolve to a ServerMetadataHandle when the send
  // is completed.
  //
  // If convert_to_cancellation is true, then the status will be converted to a
  // cancellation batch instead of a trailing metadata op in a coalesced batch.
  //
  // This quirk exists as in the filter based stack upon which our transports
  // were written if a trailing metadata op were sent it always needed to be
  // paired with an initial op batch, and the transports would wait for the
  // initial metadata batch to arrive (in case of reordering up the stack).
  auto SendServerTrailingMetadata(Target target, ServerMetadataHandle metadata,
                                  bool convert_to_cancellation);

  // Returns a promise that will resolve to a StatusOr<optional<MessageHandle>>
  // when a message is received.
  // Error => non-ok status
  // End of stream => Ok, nullopt (no message)
  // Message => Ok, message
  auto ReceiveMessage(Target target);

  // Returns a promise that will resolve to a StatusOr<ClientMetadataHandle>
  // when the receive is complete.
  auto ReceiveClientInitialMetadata(Target target);

  // Returns a promise that will resolve to a StatusOr<ClientMetadataHandle>
  // when the receive is complete.
  auto ReceiveClientTrailingMetadata(Target target);

  // Returns a promise that will resolve to a StatusOr<ServerMetadataHandle>
  // when the receive is complete.
  auto ReceiveServerInitialMetadata(Target target);

  // Returns a promise that will resolve to a StatusOr<ServerMetadataHandle>
  // when the receive is complete.
  auto ReceiveServerTrailingMetadata(Target target);

  // Send a cancellation: does not occupy the same payload, nor does it
  // coalesce with other ops.
  void Cancel(Target target, absl::Status status);

 private:
  struct Batch;

  // Base pending operation
  struct PendingCompletion {
    explicit PendingCompletion(RefCountedPtr<Batch> batch);
    virtual absl::string_view name() const = 0;
    static void CompletionCallback(void* self, grpc_error_handle error);
    grpc_closure on_done_closure;
    Latch<absl::Status> done_latch;
    RefCountedPtr<Batch> batch;

   protected:
    ~PendingCompletion() = default;
  };

  // A pending receive message.
  struct PendingReceiveMessage final : public PendingCompletion {
    using PendingCompletion::PendingCompletion;

    absl::string_view name() const override { return "receive_message"; }

    MessageHandle IntoMessageHandle() {
      return GetContext<Arena>()->MakePooled<Message>(std::move(*payload),
                                                      flags);
    }

    absl::optional<SliceBuffer> payload;
    uint32_t flags;
    bool call_failed_before_recv_message = false;
  };

  // A pending receive metadata.
  struct PendingReceiveMetadata : public PendingCompletion {
    using PendingCompletion::PendingCompletion;

    Arena::PoolPtr<grpc_metadata_batch> metadata =
        GetContext<Arena>()->MakePooled<grpc_metadata_batch>(
            GetContext<Arena>());

   protected:
    ~PendingReceiveMetadata() = default;
  };

  struct PendingReceiveInitialMetadata final : public PendingReceiveMetadata {
    using PendingReceiveMetadata::PendingReceiveMetadata;
    absl::string_view name() const override {
      return "receive_initial_metadata";
    }
  };

  struct PendingReceiveTrailingMetadata final : public PendingReceiveMetadata {
    using PendingReceiveMetadata::PendingReceiveMetadata;
    absl::string_view name() const override {
      return "receive_trailing_metadata";
    }
  };

  // Pending sends in a batch
  struct PendingSends final : public PendingCompletion {
    using PendingCompletion::PendingCompletion;

    absl::string_view name() const override { return "sends"; }

    MessageHandle send_message;
    Arena::PoolPtr<grpc_metadata_batch> send_initial_metadata;
    Arena::PoolPtr<grpc_metadata_batch> send_trailing_metadata;
    bool trailing_metadata_sent = false;
  };

  // One outstanding batch.
  struct Batch final {
    Batch(grpc_transport_stream_op_batch_payload* payload,
          grpc_stream_refcount* stream_refcount);
    ~Batch();
    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    void IncrementRefCount() { ++refs; }
    void Unref() {
      if (--refs == 0) party->arena()->DeletePooled(this);
    }
    RefCountedPtr<Batch> Ref() {
      IncrementRefCount();
      return RefCountedPtr<Batch>(this);
    }
    // Get an initialized pending completion.
    // There are four pending completions potentially contained within a batch.
    // They can be rather large so we don't create all of them always. Instead,
    // we dynamically create them on the arena as needed.
    // This method either returns the existing completion in a batch if that
    // completion has already been initialized, or it creates a new completion
    // and returns that.
    template <typename T>
    T* GetInitializedCompletion(T*(Batch::*field)) {
      if (this->*field != nullptr) return this->*field;
      this->*field = party->arena()->NewPooled<T>(Ref());
      if (grpc_call_trace.enabled()) {
        gpr_log(GPR_DEBUG, "%s[connected] Add batch closure for %s @ %s",
                Activity::current()->DebugTag().c_str(),
                std::string((this->*field)->name()).c_str(),
                (this->*field)->on_done_closure.DebugString().c_str());
      }
      return this->*field;
    }
    // grpc_transport_perform_stream_op on target.stream
    void PerformWith(Target target);
    // Take a promise, and return a promise that holds a ref on this batch until
    // the promise completes or is cancelled.
    template <typename P>
    auto RefUntil(P promise) {
      return [self = Ref(), promise = std::move(promise)]() mutable {
        return promise();
      };
    }

    grpc_transport_stream_op_batch batch;
    PendingReceiveMessage* pending_receive_message = nullptr;
    PendingReceiveInitialMetadata* pending_receive_initial_metadata = nullptr;
    PendingReceiveTrailingMetadata* pending_receive_trailing_metadata = nullptr;
    PendingSends* pending_sends = nullptr;
    const RefCountedPtr<Party> party;
    grpc_stream_refcount* const stream_refcount;
    uint8_t refs = 0;
  };

  // Get a batch for the given target.
  // Currently: if the current batch is for this target, return it - otherwise
  // flush the batch and start a new one (and return that).
  // This function may change in the future to allow multiple batches to be
  // building at once (if that turns out to be useful for hedging).
  Batch* GetBatch(Target target);
  // Flush the current batch down to the transport.
  void FlushBatch();
  // Create a cancel batch with its own payload.
  Batch* MakeCancel(grpc_stream_refcount* stream_refcount, absl::Status status);

  // Note: we don't distinguish between client and server metadata here.
  // At the time of writing they're both the same thing - and it's unclear
  // whether we'll get to separate them prior to batches going away or not.
  // So for now we claim YAGNI and just do the simplest possible implementation.
  auto SendInitialMetadata(Target target,
                           Arena::PoolPtr<grpc_metadata_batch> md);
  auto ReceiveInitialMetadata(Target target);
  auto ReceiveTrailingMetadata(Target target);

  // Combine send status and server metadata into a final status to report back
  // to the containing call.
  static ServerMetadataHandle CompleteSendServerTrailingMetadata(
      ServerMetadataHandle sent_metadata, absl::Status send_result,
      bool actually_sent);

  grpc_transport_stream_op_batch_payload* const payload_;
  absl::optional<Target> target_;
  Batch* batch_ = nullptr;
};

inline auto BatchBuilder::SendMessage(Target target, MessageHandle message) {
  auto* batch = GetBatch(target);
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s[connected] [batch %p] Queue send message: %s",
            Activity::current()->DebugTag().c_str(), batch,
            message->DebugString().c_str());
  }
  auto* pc = batch->GetInitializedCompletion(&Batch::pending_sends);
  batch->batch.on_complete = &pc->on_done_closure;
  batch->batch.send_message = true;
  payload_->send_message.send_message = message->payload();
  payload_->send_message.flags = message->flags();
  pc->send_message = std::move(message);
  return batch->RefUntil(pc->done_latch.WaitAndCopy());
}

inline auto BatchBuilder::SendInitialMetadata(
    Target target, Arena::PoolPtr<grpc_metadata_batch> md) {
  auto* batch = GetBatch(target);
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "%s[connected] [batch %p] Queue send initial metadata: %s",
            Activity::current()->DebugTag().c_str(), batch,
            md->DebugString().c_str());
  }
  auto* pc = batch->GetInitializedCompletion(&Batch::pending_sends);
  batch->batch.on_complete = &pc->on_done_closure;
  batch->batch.send_initial_metadata = true;
  payload_->send_initial_metadata.send_initial_metadata = md.get();
  pc->send_initial_metadata = std::move(md);
  return batch->RefUntil(pc->done_latch.WaitAndCopy());
}

inline auto BatchBuilder::SendClientInitialMetadata(
    Target target, ClientMetadataHandle metadata) {
  return SendInitialMetadata(target, std::move(metadata));
}

inline auto BatchBuilder::SendClientTrailingMetadata(Target target) {
  auto* batch = GetBatch(target);
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s[connected] [batch %p] Queue send trailing metadata",
            Activity::current()->DebugTag().c_str(), batch);
  }
  auto* pc = batch->GetInitializedCompletion(&Batch::pending_sends);
  batch->batch.on_complete = &pc->on_done_closure;
  batch->batch.send_trailing_metadata = true;
  auto metadata =
      GetContext<Arena>()->MakePooled<grpc_metadata_batch>(GetContext<Arena>());
  payload_->send_trailing_metadata.send_trailing_metadata = metadata.get();
  payload_->send_trailing_metadata.sent = nullptr;
  pc->send_trailing_metadata = std::move(metadata);
  return batch->RefUntil(pc->done_latch.WaitAndCopy());
}

inline auto BatchBuilder::SendServerInitialMetadata(
    Target target, ServerMetadataHandle metadata) {
  return SendInitialMetadata(target, std::move(metadata));
}

inline auto BatchBuilder::SendServerTrailingMetadata(
    Target target, ServerMetadataHandle metadata,
    bool convert_to_cancellation) {
  Batch* batch;
  PendingSends* pc;
  if (convert_to_cancellation) {
    const auto status_code =
        metadata->get(GrpcStatusMetadata()).value_or(GRPC_STATUS_UNKNOWN);
    auto status = grpc_error_set_int(
        absl::Status(static_cast<absl::StatusCode>(status_code),
                     metadata->GetOrCreatePointer(GrpcMessageMetadata())
                         ->as_string_view()),
        StatusIntProperty::kRpcStatus, status_code);
    batch = MakeCancel(target.stream_refcount, std::move(status));
    pc = batch->GetInitializedCompletion(&Batch::pending_sends);
  } else {
    batch = GetBatch(target);
    pc = batch->GetInitializedCompletion(&Batch::pending_sends);
    batch->batch.send_trailing_metadata = true;
    payload_->send_trailing_metadata.send_trailing_metadata = metadata.get();
    payload_->send_trailing_metadata.sent = &pc->trailing_metadata_sent;
  }
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s[connected] [batch %p] %s: %s",
            Activity::current()->DebugTag().c_str(), batch,
            convert_to_cancellation ? "Send trailing metadata as cancellation"
                                    : "Queue send trailing metadata",
            metadata->DebugString().c_str());
  }
  batch->batch.on_complete = &pc->on_done_closure;
  pc->send_trailing_metadata = std::move(metadata);
  auto promise = batch->RefUntil(
      Map(pc->done_latch.WaitAndCopy(), [pc](absl::Status status) {
        return CompleteSendServerTrailingMetadata(
            std::move(pc->send_trailing_metadata), std::move(status),
            pc->trailing_metadata_sent);
      }));
  if (convert_to_cancellation) {
    batch->PerformWith(target);
  }
  return promise;
}

inline auto BatchBuilder::ReceiveMessage(Target target) {
  auto* batch = GetBatch(target);
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s[connected] [batch %p] Queue receive message",
            Activity::current()->DebugTag().c_str(), batch);
  }
  auto* pc = batch->GetInitializedCompletion(&Batch::pending_receive_message);
  batch->batch.recv_message = true;
  payload_->recv_message.recv_message_ready = &pc->on_done_closure;
  payload_->recv_message.recv_message = &pc->payload;
  payload_->recv_message.flags = &pc->flags;
  payload_->recv_message.call_failed_before_recv_message =
      &pc->call_failed_before_recv_message;
  return batch->RefUntil(
      Map(pc->done_latch.Wait(),
          [pc](absl::Status status)
              -> absl::StatusOr<absl::optional<MessageHandle>> {
            if (!status.ok()) return status;
            if (!pc->payload.has_value()) {
              if (pc->call_failed_before_recv_message) {
                return absl::CancelledError();
              }
              return absl::nullopt;
            }
            return pc->IntoMessageHandle();
          }));
}

inline auto BatchBuilder::ReceiveInitialMetadata(Target target) {
  auto* batch = GetBatch(target);
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "%s[connected] [batch %p] Queue receive initial metadata",
            Activity::current()->DebugTag().c_str(), batch);
  }
  auto* pc =
      batch->GetInitializedCompletion(&Batch::pending_receive_initial_metadata);
  batch->batch.recv_initial_metadata = true;
  payload_->recv_initial_metadata.recv_initial_metadata_ready =
      &pc->on_done_closure;
  payload_->recv_initial_metadata.recv_initial_metadata = pc->metadata.get();
  return batch->RefUntil(
      Map(pc->done_latch.Wait(),
          [pc](absl::Status status) -> absl::StatusOr<ClientMetadataHandle> {
            if (!status.ok()) return status;
            return std::move(pc->metadata);
          }));
}

inline auto BatchBuilder::ReceiveClientInitialMetadata(Target target) {
  return ReceiveInitialMetadata(target);
}

inline auto BatchBuilder::ReceiveServerInitialMetadata(Target target) {
  return ReceiveInitialMetadata(target);
}

inline auto BatchBuilder::ReceiveTrailingMetadata(Target target) {
  auto* batch = GetBatch(target);
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "%s[connected] [batch %p] Queue receive trailing metadata",
            Activity::current()->DebugTag().c_str(), batch);
  }
  auto* pc = batch->GetInitializedCompletion(
      &Batch::pending_receive_trailing_metadata);
  batch->batch.recv_trailing_metadata = true;
  payload_->recv_trailing_metadata.recv_trailing_metadata_ready =
      &pc->on_done_closure;
  payload_->recv_trailing_metadata.recv_trailing_metadata = pc->metadata.get();
  payload_->recv_trailing_metadata.collect_stats =
      &GetContext<CallContext>()->call_stats()->transport_stream_stats;
  return batch->RefUntil(
      Map(pc->done_latch.Wait(),
          [pc](absl::Status status) -> absl::StatusOr<ServerMetadataHandle> {
            if (!status.ok()) return status;
            return std::move(pc->metadata);
          }));
}

inline auto BatchBuilder::ReceiveClientTrailingMetadata(Target target) {
  return ReceiveTrailingMetadata(target);
}

inline auto BatchBuilder::ReceiveServerTrailingMetadata(Target target) {
  return ReceiveTrailingMetadata(target);
}

template <>
struct ContextType<BatchBuilder> {};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_BATCH_BUILDER_H
