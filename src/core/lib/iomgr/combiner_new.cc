//
// Copyright 2019 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/combiner_new.h"

namespace grpc_core {

DebugOnlyTraceFlag grpc_combiner_new_trace(false, "combiner_new");

struct WorkItem {
  explicit WorkItem(std::function<void()> callback)
      : callback(std::move(callback)) {}

  MultiProducerSingleConsumerQueue::Node mpscq_node;
  std::function<void()> callback;
};

void Combiner::Schedule(std::function<void()> callback,
                        DebugLocation location, const char* reason) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
    gpr_log(GPR_INFO, "==> Combiner::Schedule() [%p] %s:%d: %s", this,
            location.file(), location.line(), reason);
  }
  const size_t prev_size = size_.FetchAdd(1);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
    gpr_log(GPR_INFO, "  size: %" PRIdPTR " -> %" PRIdPTR, prev_size,
            prev_size + 1);
  }
  if (prev_size == 0) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
      gpr_log(GPR_INFO, "  EXECUTING IMMEDIATELY");
    }
    // Queue was empty, so execute the callback immediately.
    callback();
    // If any new callbacks were added while we were executing this one,
    // execute them now.
    DrainQueue();
  } else {
    // Queue was not empty, so add closure to queue.
    WorkItem* work_item = New<WorkItem>(std::move(callback));
    if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
      gpr_log(GPR_INFO, "  QUEUING work_item=%p", work_item);
    }
    queue_.Push(&work_item->mpscq_node);
  }
}

void Combiner::DrainQueue() {
  while (true) {
    if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
      gpr_log(GPR_INFO, "==> Combiner::DrainQueue() [%p]", this);
    }
    size_t prev_size = size_.FetchSub(1);
    if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
      gpr_log(GPR_INFO, "  size: %" PRIdPTR " -> %" PRIdPTR, prev_size,
              prev_size - 1);
    }
    GPR_ASSERT(prev_size >= 1);
    if (prev_size == 0) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
        gpr_log(GPR_INFO, "  queue empty -- yielding combiner");
      }
      break;
    }
    while (true) {
      if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
        gpr_log(GPR_INFO, "  checking queue");
      }
      bool empty;
      WorkItem* work_item =
          reinterpret_cast<WorkItem*>(queue_.PopAndCheckEnd(&empty));
      if (work_item == nullptr) {
        // This can happen either due to a race condition within the mpscq
        // code or because of a race with Schedule().
        if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
          gpr_log(GPR_INFO, "  queue returned no result; checking again");
        }
        continue;
      }
      if (GRPC_TRACE_FLAG_ENABLED(grpc_combiner_new_trace)) {
        gpr_log(GPR_INFO, "  EXECUTING FROM QUEUE: work_item=%p", work_item);
      }
      work_item->callback();
      Delete(work_item);
      break;
    }
  }
}

}  // namespace grpc_core
