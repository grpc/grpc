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

#include "src/core/client_channel/buffered_call.h"

namespace grpc_core {

BufferedCall::BufferedCall(CallCombiner* call_combiner, TraceFlag* tracer)
    : call_combiner_(call_combiner), tracer_(tracer) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    LOG(INFO) << "BufferedCall " << this << ": created";
  }
}

BufferedCall::~BufferedCall() {
  // Make sure there are no remaining pending batches.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    GRPC_CHECK_EQ(pending_batches_[i], nullptr);
  }
}

size_t BufferedCall::GetBatchIndex(grpc_transport_stream_op_batch* batch) {
  // Note: It is important the send_initial_metadata be the first entry
  // here, since the code in CheckResolution() assumes it will be.
  if (batch->send_initial_metadata) return 0;
  if (batch->send_message) return 1;
  if (batch->send_trailing_metadata) return 2;
  if (batch->recv_initial_metadata) return 3;
  if (batch->recv_message) return 4;
  if (batch->recv_trailing_metadata) return 5;
  GPR_UNREACHABLE_CODE(return (size_t)-1);
}

// This is called via the call combiner, so access to calld is synchronized.
void BufferedCall::EnqueueBatch(grpc_transport_stream_op_batch* batch) {
  const size_t idx = GetBatchIndex(batch);
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    LOG(INFO) << "BufferedCall " << this << ": adding pending batch at index "
              << idx;
  }
  grpc_transport_stream_op_batch*& pending = pending_batches_[idx];
  GRPC_CHECK_EQ(pending, nullptr);
  pending = batch;
}

// This is called via the call combiner, so access to calld is synchronized.
void BufferedCall::FailPendingBatchInCallCombiner(void* arg,
                                                  grpc_error_handle error) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* call = static_cast<BufferedCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  grpc_transport_stream_op_batch_finish_with_failure(batch, error,
                                                     call->call_combiner_);
}

// This is called via the call combiner, so access to calld is synchronized.
void BufferedCall::Fail(
    grpc_error_handle error,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  GRPC_CHECK(!error.ok());
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    LOG(INFO) << "BufferedCall " << this << ": failing " << num_batches
              << " pending batches: " << StatusToString(error);
  }
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        FailPendingBatchInCallCombiner, batch, nullptr);
      closures.Add(&batch->handler_private.closure, error,
                   "BufferedCall::Fail");
      batch = nullptr;
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(call_combiner_);
  } else {
    closures.RunClosuresWithoutYielding(call_combiner_);
  }
}

// This is called via the call combiner, so access to calld is synchronized.
void BufferedCall::ResumePendingBatchInCallCombiner(
    void* arg, grpc_error_handle /*ignored*/) {
  grpc_transport_stream_op_batch* batch =
      static_cast<grpc_transport_stream_op_batch*>(arg);
  auto* call = static_cast<BufferedCall*>(batch->handler_private.extra_arg);
  // Note: This will release the call combiner.
  call->start_batch_(batch);
}

// This is called via the call combiner, so access to calld is synchronized.
void BufferedCall::Resume(
    absl::AnyInvocable<void(grpc_transport_stream_op_batch*)> start_batch,
    YieldCallCombinerPredicate yield_call_combiner_predicate) {
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*tracer_)) {
    size_t num_batches = 0;
    for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
      if (pending_batches_[i] != nullptr) ++num_batches;
    }
    LOG(INFO) << "BufferedCall " << this << ": starting " << num_batches
              << " pending batches";
  }
  start_batch_ = std::move(start_batch);
  CallCombinerClosureList closures;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(pending_batches_); ++i) {
    grpc_transport_stream_op_batch*& batch = pending_batches_[i];
    if (batch != nullptr) {
      batch->handler_private.extra_arg = this;
      GRPC_CLOSURE_INIT(&batch->handler_private.closure,
                        ResumePendingBatchInCallCombiner, batch, nullptr);
      closures.Add(&batch->handler_private.closure, absl::OkStatus(),
                   "resuming pending batch from client channel call");
      batch = nullptr;
    }
  }
  if (yield_call_combiner_predicate(closures)) {
    closures.RunClosures(call_combiner_);
  } else {
    closures.RunClosuresWithoutYielding(call_combiner_);
  }
}

}  // namespace grpc_core
