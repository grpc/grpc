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

#include <grpc/grpc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/completion_queue_tag.h>
#include <grpcpp/impl/grpc_library.h>

#include <vector>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/sync.h"
#include "src/core/util/thd.h"
#include "src/core/util/useful.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"

namespace grpc {

// 'CompletionQueue' constructor can safely call GrpcLibraryCodegen(false) here
// i.e not have GrpcLibraryCodegen call grpc_init(). This is because, to create
// a 'grpc_completion_queue' instance (which is being passed as the input to
// this constructor), one must have already called grpc_init().
CompletionQueue::CompletionQueue(grpc_completion_queue* take)
    : GrpcLibrary(false), cq_(take) {
  InitialAvalanching();
}

void CompletionQueue::Shutdown() {
#ifndef NDEBUG
  if (!ServerListEmpty()) {
    LOG(ERROR) << "CompletionQueue shutdown being shutdown before its server.";
  }
#endif
  CompleteAvalanching();
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
        auto core_cq_tag =
            static_cast<grpc::internal::CompletionQueueTag*>(ev.tag);
        *ok = ev.success != 0;
        *tag = core_cq_tag;
        if (core_cq_tag->FinalizeResult(tag, ok)) {
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
  GRPC_CHECK(flushed_);
}

bool CompletionQueue::CompletionQueueTLSCache::Flush(void** tag, bool* ok) {
  int res = 0;
  void* res_tag;
  flushed_ = true;
  if (grpc_completion_queue_thread_local_cache_flush(cq_->cq_, &res_tag,
                                                     &res)) {
    auto core_cq_tag =
        static_cast<grpc::internal::CompletionQueueTag*>(res_tag);
    *ok = res == 1;
    if (core_cq_tag->FinalizeResult(tag, ok)) {
      return true;
    }
  }
  return false;
}

}  // namespace grpc
