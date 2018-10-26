/*
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

#include <grpcpp/completion_queue.h>

#include <memory>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/time.h>

namespace grpc {

static internal::GrpcLibraryInitializer g_gli_initializer;

// 'CompletionQueue' constructor can safely call GrpcLibraryCodegen(false) here
// i.e not have GrpcLibraryCodegen call grpc_init(). This is because, to create
// a 'grpc_completion_queue' instance (which is being passed as the input to
// this constructor), one must have already called grpc_init().
CompletionQueue::CompletionQueue(grpc_completion_queue* take)
    : GrpcLibraryCodegen(false), cq_(take) {
  InitialAvalanching();
}

void CompletionQueue::Shutdown() {
  g_gli_initializer.summon();
  CompleteAvalanching();
}

void CompletionQueue::CompleteAvalanching() {
  // Check if this was the last avalanching operation
  if (gpr_atm_no_barrier_fetch_add(&avalanches_in_flight_,
                                   static_cast<gpr_atm>(-1)) == 1) {
    grpc_completion_queue_shutdown(cq_);
  }
}

CompletionQueue::NextStatus CompletionQueue::AsyncNextInternal(
    void** tag, bool* ok, gpr_timespec deadline) {
  for (;;) {
    auto ev = grpc_completion_queue_next(cq_, deadline, nullptr);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        return TIMEOUT;
      case GRPC_QUEUE_SHUTDOWN:
        return SHUTDOWN;
      case GRPC_OP_COMPLETE:
        auto cq_tag = static_cast<internal::CompletionQueueTag*>(ev.tag);
        *ok = ev.success != 0;
        *tag = cq_tag;
        if (cq_tag->FinalizeResult(tag, ok)) {
          return GOT_EVENT;
        }
        break;
    }
  }
}

CompletionQueue::CompletionQueueTLSCache::CompletionQueueTLSCache(
    CompletionQueue* cq)
    : cq_(cq), flushed_(false) {
  grpc_completion_queue_thread_local_cache_init(cq_->cq_);
}

CompletionQueue::CompletionQueueTLSCache::~CompletionQueueTLSCache() {
  GPR_ASSERT(flushed_);
}

bool CompletionQueue::CompletionQueueTLSCache::Flush(void** tag, bool* ok) {
  int res = 0;
  void* res_tag;
  flushed_ = true;
  if (grpc_completion_queue_thread_local_cache_flush(cq_->cq_, &res_tag,
                                                     &res)) {
    auto cq_tag = static_cast<internal::CompletionQueueTag*>(res_tag);
    *ok = res == 1;
    if (cq_tag->FinalizeResult(tag, ok)) {
      return true;
    }
  }
  return false;
}

}  // namespace grpc
