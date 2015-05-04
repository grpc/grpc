/*
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/completion_queue.h>

#include <memory>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc++/time.h>

namespace grpc {

CompletionQueue::CompletionQueue() { cq_ = grpc_completion_queue_create(); }

CompletionQueue::CompletionQueue(grpc_completion_queue* take) : cq_(take) {}

CompletionQueue::~CompletionQueue() { grpc_completion_queue_destroy(cq_); }

void CompletionQueue::Shutdown() { grpc_completion_queue_shutdown(cq_); }

CompletionQueue::NextStatus CompletionQueue::AsyncNextInternal(
    void** tag, bool* ok, gpr_timespec deadline) {
  for (;;) {
    auto ev = grpc_completion_queue_next(cq_, deadline);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        return TIMEOUT;
      case GRPC_QUEUE_SHUTDOWN:
        return SHUTDOWN;
      case GRPC_OP_COMPLETE:
        auto cq_tag = static_cast<CompletionQueueTag*>(ev.tag);
        *ok = ev.success != 0;
        *tag = cq_tag;
        if (cq_tag->FinalizeResult(tag, ok)) {
          return GOT_EVENT;
        }
        break;
    }
  }
}

bool CompletionQueue::Pluck(CompletionQueueTag* tag) {
  auto ev = grpc_completion_queue_pluck(cq_, tag, gpr_inf_future);
  bool ok = ev.success != 0;
  void* ignored = tag;
  GPR_ASSERT(tag->FinalizeResult(&ignored, &ok));
  GPR_ASSERT(ignored == tag);
  // Ignore mutations by FinalizeResult: Pluck returns the C API status
  return ev.success != 0;
}

void CompletionQueue::TryPluck(CompletionQueueTag* tag) {
  auto ev = grpc_completion_queue_pluck(cq_, tag, gpr_time_0);
  if (ev.type == GRPC_QUEUE_TIMEOUT) return;
  bool ok = ev.success != 0;
  void* ignored = tag;
  // the tag must be swallowed if using TryPluck
  GPR_ASSERT(!tag->FinalizeResult(&ignored, &ok));
}

}  // namespace grpc
