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

// Helper class so we can declare a unique_ptr with grpc_event
class EventDeleter {
 public:
  void operator()(grpc_event* ev) {
    if (ev) grpc_event_finish(ev);
  }
};

CompletionQueue::NextStatus CompletionQueue::AsyncNextInternal(
    void** tag, bool* ok, gpr_timespec deadline) {
  std::unique_ptr<grpc_event, EventDeleter> ev;

  for (;;) {
    ev.reset(grpc_completion_queue_next(cq_, deadline));
    if (!ev) { /* got a NULL back because deadline passed */
      return TIMEOUT;
    }
    if (ev->type == GRPC_QUEUE_SHUTDOWN) {
      return SHUTDOWN;
    }
    auto cq_tag = static_cast<CompletionQueueTag*>(ev->tag);
    *ok = ev->data.op_complete == GRPC_OP_OK;
    *tag = cq_tag;
    if (cq_tag->FinalizeResult(tag, ok)) {
      return GOT_EVENT;
    }
  }
}

bool CompletionQueue::Pluck(CompletionQueueTag* tag) {
  std::unique_ptr<grpc_event, EventDeleter> ev;

  ev.reset(grpc_completion_queue_pluck(cq_, tag, gpr_inf_future));
  bool ok = ev->data.op_complete == GRPC_OP_OK;
  void* ignored = tag;
  GPR_ASSERT(tag->FinalizeResult(&ignored, &ok));
  GPR_ASSERT(ignored == tag);
  // Ignore mutations by FinalizeResult: Pluck returns the C API status
  return ev->data.op_complete == GRPC_OP_OK;
}

void CompletionQueue::TryPluck(CompletionQueueTag* tag) {
  std::unique_ptr<grpc_event, EventDeleter> ev;

  ev.reset(grpc_completion_queue_pluck(cq_, tag, gpr_time_0));
  if (!ev) return;
  bool ok = ev->data.op_complete == GRPC_OP_OK;
  void* ignored = tag;
  // the tag must be swallowed if using TryPluck
  GPR_ASSERT(!tag->FinalizeResult(&ignored, &ok));
}

}  // namespace grpc
