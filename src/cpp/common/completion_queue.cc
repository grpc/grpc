/*
 *
 * Copyright 2014, Google Inc.
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

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/cpp/util/time.h"
#include <grpc++/async_server_context.h>

namespace grpc {

CompletionQueue::CompletionQueue() { cq_ = grpc_completion_queue_create(); }

CompletionQueue::~CompletionQueue() { grpc_completion_queue_destroy(cq_); }

void CompletionQueue::Shutdown() { grpc_completion_queue_shutdown(cq_); }

CompletionQueue::CompletionType CompletionQueue::Next(void **tag) {
  grpc_event *ev;
  CompletionType return_type;
  bool success;

  ev = grpc_completion_queue_next(cq_, gpr_inf_future);
  if (!ev) {
    gpr_log(GPR_ERROR, "no next event in queue");
    abort();
  }
  switch (ev->type) {
    case GRPC_QUEUE_SHUTDOWN:
      return_type = QUEUE_CLOSED;
      break;
    case GRPC_READ:
      *tag = ev->tag;
      if (ev->data.read) {
        success = static_cast<AsyncServerContext *>(ev->tag)
                      ->ParseRead(ev->data.read);
        return_type = success ? SERVER_READ_OK : SERVER_READ_ERROR;
      } else {
        return_type = SERVER_READ_ERROR;
      }
      break;
    case GRPC_WRITE_ACCEPTED:
      *tag = ev->tag;
      if (ev->data.write_accepted != GRPC_OP_ERROR) {
        return_type = SERVER_WRITE_OK;
      } else {
        return_type = SERVER_WRITE_ERROR;
      }
      break;
    case GRPC_SERVER_RPC_NEW:
      GPR_ASSERT(!ev->tag);
      // Finishing the pending new rpcs after the server has been shutdown.
      if (!ev->call) {
        *tag = nullptr;
      } else {
        *tag = new AsyncServerContext(
            ev->call, ev->data.server_rpc_new.method,
            ev->data.server_rpc_new.host,
            Timespec2Timepoint(ev->data.server_rpc_new.deadline));
      }
      return_type = SERVER_RPC_NEW;
      break;
    case GRPC_FINISHED:
      *tag = ev->tag;
      return_type = RPC_END;
      break;
    case GRPC_FINISH_ACCEPTED:
      *tag = ev->tag;
      return_type = HALFCLOSE_OK;
      break;
    default:
      // We do not handle client side messages now
      gpr_log(GPR_ERROR, "client-side messages aren't supported yet");
      abort();
  }
  grpc_event_finish(ev);
  return return_type;
}

}  // namespace grpc
