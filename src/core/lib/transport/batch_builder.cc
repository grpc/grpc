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

void BatchBuilder::PendingCompletion::CompletionCallback(
    void* self, grpc_error_handle error) {
  auto* pc = static_cast<PendingCompletion*>(self);
  RefCountedPtr<Batch> batch = std::exchange(pc->batch, nullptr);
  auto* party = batch->party.get();
  if (grpc_call_trace.enabled()) {
    gpr_log(GPR_DEBUG, "%s[connected] Finish batch %s: status=%s",
            party->DebugTag().c_str(),
            grpc_transport_stream_op_batch_string(&batch->batch).c_str(),
            status.ToString().c_str());
  }
  party->Spawn(
      name,
      [batch = std::move(batch), error = std::move(error)]() mutable {
        batch->done_latch.Set(std::move(error));
        return Empty{};
      },
      [](Empty) {});
}

BatchBuilder::PendingCompletion::PendingCompletion(RefCountedPtr<Batch> batch,
                                                   absl::string_view name)
    : batch(std::move(batch)) {
  GRPC_CLOSURE_INIT(&on_done_closure, CompletionCallback, this, nullptr);
}

BatchBuilder::Batch::Batch(grpc_transport_stream_op_batch_payload* payload,
                           grpc_stream_refcount* stream_refcount)
    : party(static_cast<Party*>(Activity::current())->Ref()),
      stream_refcount(stream_refcount) {
  memset(&batch, 0, sizeof(batch));
  batch.payload = payload;
  grpc_stream_ref(stream_refcount);
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
  grpc_stream_unref(stream_refcount);
}

Batch* BatchBuilder::GetBatch(Target target) {
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
  std::exchange(batch_, nullptr)->Perform(*target_);
  target_.reset();
}

void BatchBuilder::Batch::Perform(Target target) {
  grpc_transport_perform_stream_op(transport_, stream_, &batch);
}

ServerMetadataHandle BatchBuilder::CompleteSendServerTrailingMetadata(
    ServerMetadataHandle sent_metadata, absl::Status send_result, bool sent) {
  if (!status.ok()) {
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "%s[connected] Send metadata failed with error: %s, "
              "fabricating trailing metadata",
              Activity::current()->DebugTag().c_str(),
              status.status().ToString().c_str());
    }
    sent_metadata->Clear();
    sent_metadata->Set(GrpcStatusMetadata(),
                       static_cast<grpc_status_code>(status.status().code()));
    sent_metadata->Set(GrpcMessageMetadata(),
                       Slice::FromCopiedString(status.status().message()));
    sent_metadata->Set(GrpcCallWasCancelled(), true);
  }
  if (!sent_metadata->get(GrpcCallWasCancelled()).has_value()) {
    if (grpc_call_trace.enabled()) {
      gpr_log(GPR_DEBUG,
              "%s[connected] Tagging trailing metadata with "
              "cancellation status from transport: %s",
              Activity::current()->DebugTag().c_str(),
              call_data->sent_trailing_metadata ? "sent => not-cancelled"
                                                : "not-sent => cancelled");
    }
    sent_metadata->Set(GrpcCallWasCancelled(), !sent);
  }
  return sent_metadata;
}

}  // namespace grpc_core
