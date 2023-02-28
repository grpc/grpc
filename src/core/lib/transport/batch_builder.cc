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

#include "src/core/lib/transport/batch_builder.h"

#include "batch_builder.h"
#include "transport.h"

#include "src/core/lib/surface/call_trace.h"

namespace grpc_core {

BatchBuilder::BatchBuilder(grpc_transport_stream_op_batch_payload* payload)
    : payload_(payload) {}

void BatchBuilder::PendingCompletion::CompletionCallback(
    void* self, grpc_error_handle error) {
  auto* pc = static_cast<PendingCompletion*>(self);
  auto* party = pc->batch->party.get();
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG,
            "%s[connected] Finish batch-component %s for %s: status=%s",
            party->DebugTag().c_str(), std::string(pc->name()).c_str(),
            grpc_transport_stream_op_batch_string(&pc->batch->batch).c_str(),
            error.ToString().c_str());
  }
  party->Spawn(
      "batch-completion",
      [pc, error = std::move(error)]() mutable {
        RefCountedPtr<Batch> batch = std::exchange(pc->batch, nullptr);
        pc->done_latch.Set(std::move(error));
        return Empty{};
      },
      [](Empty) {});
}

BatchBuilder::PendingCompletion::PendingCompletion(RefCountedPtr<Batch> batch)
    : batch(std::move(batch)) {
  GRPC_CLOSURE_INIT(&on_done_closure, CompletionCallback, this, nullptr);
}

BatchBuilder::Batch::Batch(grpc_transport_stream_op_batch_payload* payload,
                           grpc_stream_refcount* stream_refcount)
    : party(static_cast<Party*>(Activity::current())->Ref()),
      stream_refcount(stream_refcount) {
  memset(&batch, 0, sizeof(batch));
  batch.payload = payload;
#ifndef NDEBUG
  grpc_stream_ref(stream_refcount, "pending-batch");
#else
  grpc_stream_ref(stream_refcount);
#endif
}

BatchBuilder::Batch::~Batch() {
  auto* arena = party->arena();
  if (pending_receive_message != nullptr) {
    arena->DeletePooled(pending_receive_message);
  }
  if (pending_receive_initial_metadata != nullptr) {
    arena->DeletePooled(pending_receive_initial_metadata);
  }
  if (pending_receive_trailing_metadata != nullptr) {
    arena->DeletePooled(pending_receive_trailing_metadata);
  }
  if (pending_sends != nullptr) {
    arena->DeletePooled(pending_sends);
  }
  if (batch.cancel_stream) {
    arena->DeletePooled(batch.payload);
  }
#ifndef NDEBUG
  grpc_stream_unref(stream_refcount, "pending-batch");
#else
  grpc_stream_unref(stream_refcount);
#endif
}

BatchBuilder::Batch* BatchBuilder::GetBatch(Target target) {
  if (target_.has_value() && target_->stream != target.stream) {
    FlushBatch();
  }
  if (!target_.has_value()) {
    target_ = target;
    batch_ = GetContext<Arena>()->NewPooled<Batch>(payload_,
                                                   target_->stream_refcount);
  }
  GPR_ASSERT(batch_ != nullptr);
  return batch_;
}

void BatchBuilder::FlushBatch() {
  GPR_ASSERT(batch_ != nullptr);
  GPR_ASSERT(target_.has_value());
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s[connected] Perform transport stream op batch: %s",
            batch_->party->DebugTag().c_str(),
            grpc_transport_stream_op_batch_string(&batch_->batch).c_str());
  }
  std::exchange(batch_, nullptr)->PerformWith(*target_);
  target_.reset();
}

void BatchBuilder::Batch::PerformWith(Target target) {
  grpc_transport_perform_stream_op(target.transport, target.stream, &batch);
}

ServerMetadataHandle BatchBuilder::CompleteSendServerTrailingMetadata(
    ServerMetadataHandle sent_metadata, absl::Status send_result,
    bool actually_sent) {
  if (!send_result.ok()) {
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "%s[connected] Send metadata failed with error: %s, "
              "fabricating trailing metadata",
              Activity::current()->DebugTag().c_str(),
              send_result.ToString().c_str());
    }
    sent_metadata->Clear();
    sent_metadata->Set(GrpcStatusMetadata(),
                       static_cast<grpc_status_code>(send_result.code()));
    sent_metadata->Set(GrpcMessageMetadata(),
                       Slice::FromCopiedString(send_result.message()));
    sent_metadata->Set(GrpcCallWasCancelled(), true);
  }
  if (!sent_metadata->get(GrpcCallWasCancelled()).has_value()) {
    if (grpc_call_trace.enabled()) {
      gpr_log(
          GPR_DEBUG,
          "%s[connected] Tagging trailing metadata with "
          "cancellation status from transport: %s",
          Activity::current()->DebugTag().c_str(),
          actually_sent ? "sent => not-cancelled" : "not-sent => cancelled");
    }
    sent_metadata->Set(GrpcCallWasCancelled(), !actually_sent);
  }
  return sent_metadata;
}

BatchBuilder::Batch* BatchBuilder::MakeCancel(
    grpc_stream_refcount* stream_refcount, absl::Status status) {
  auto* arena = GetContext<Arena>();
  auto* payload =
      arena->NewPooled<grpc_transport_stream_op_batch_payload>(nullptr);
  auto* batch = arena->NewPooled<Batch>(payload, stream_refcount);
  batch->batch.cancel_stream = true;
  payload->cancel_stream.cancel_error = std::move(status);
  return batch;
}

void BatchBuilder::Cancel(Target target, absl::Status status) {
  auto* batch = MakeCancel(target.stream_refcount, std::move(status));
  batch->batch.on_complete = NewClosure(
      [batch](absl::Status) { batch->party->arena()->DeletePooled(batch); });
  batch->PerformWith(target);
}

}  // namespace grpc_core
