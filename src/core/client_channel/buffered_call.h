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

#ifndef GRPC_SRC_CORE_CLIENT_CHANNEL_BUFFERED_CALL_H
#define GRPC_SRC_CORE_CLIENT_CHANNEL_BUFFERED_CALL_H

#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "absl/functional/any_invocable.h"

// Max number of batches that can be pending on a call at any given
// time.  This includes one batch for each of the following ops:
//   recv_initial_metadata
//   send_initial_metadata
//   recv_message
//   send_message
//   recv_trailing_metadata
//   send_trailing_metadata
#define MAX_PENDING_BATCHES 6

namespace grpc_core {

// Handles queuing of stream batches for a v1 call.
class BufferedCall {
 public:
  BufferedCall(CallCombiner* call_combiner, TraceFlag* tracer);
  ~BufferedCall();

  // Enqueues a batch.
  // Must be called from within the call combiner.
  void EnqueueBatch(grpc_transport_stream_op_batch* batch);

  // Resumes all queued batches by passing them to start_batch().
  // Must be called from within the call combiner.
  void Resume(
      absl::AnyInvocable<void(grpc_transport_stream_op_batch*)> start_batch);

  // A predicate type and some useful implementations for Fail().
  typedef bool (*YieldCallCombinerPredicate)(
      const CallCombinerClosureList& closures);
  static bool YieldCallCombiner(const CallCombinerClosureList& /*closures*/) {
    return true;
  }
  static bool NoYieldCallCombiner(const CallCombinerClosureList& /*closures*/) {
    return false;
  }
  static bool YieldCallCombinerIfPendingBatchesFound(
      const CallCombinerClosureList& closures) {
    return closures.size() > 0;
  }
  // Fails all queued batches.
  // Must be called from within the call combiner.
  // If yield_call_combiner_predicate returns true, assumes responsibility for
  // yielding the call combiner.
  void Fail(grpc_error_handle error,
            YieldCallCombinerPredicate yield_call_combiner_predicate);

  grpc_metadata_batch* send_initial_metadata() const {
    return pending_batches_[0]
        ->payload->send_initial_metadata.send_initial_metadata;
  }

 private:
  // Returns the index into pending_batches_ to be used for batch.
  static size_t GetBatchIndex(grpc_transport_stream_op_batch* batch);

  static void FailPendingBatchInCallCombiner(void* arg,
                                             grpc_error_handle error);
  static void ResumePendingBatchInCallCombiner(void* arg,
                                               grpc_error_handle ignored);

  CallCombiner* const call_combiner_;
  TraceFlag* tracer_;

  // Batches are added to this list when received from above.
  // They are removed when we are done handling the batch (i.e., when
  // either we have invoked all of the batch's callbacks or we have
  // passed the batch down to the next call and are not intercepting any of
  // its callbacks).
  grpc_transport_stream_op_batch* pending_batches_[MAX_PENDING_BATCHES] = {};

  // A function that starts a batch on the next call.  Set by calling
  // Resume().
  absl::AnyInvocable<void(grpc_transport_stream_op_batch*)> start_batch_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CLIENT_CHANNEL_BUFFERED_CALL_H
