/*
 *
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/surface/channel.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/completion_queue.h"

typedef struct {
  grpc_closure closure;
  void* tag;
  grpc_completion_queue* cq;
  grpc_cq_completion completion_storage;
} ping_result;

static void ping_destroy(void* arg, grpc_cq_completion* storage) {
  gpr_free(arg);
}

static void ping_done(void* arg, grpc_error* error) {
  ping_result* pr = static_cast<ping_result*>(arg);
  grpc_cq_end_op(pr->cq, pr->tag, GRPC_ERROR_REF(error), ping_destroy, pr,
                 &pr->completion_storage);
}

void grpc_channel_ping(grpc_channel* channel, grpc_completion_queue* cq,
                       void* tag, void* reserved) {
  GRPC_API_TRACE("grpc_channel_ping(channel=%p, cq=%p, tag=%p, reserved=%p)", 4,
                 (channel, cq, tag, reserved));
  grpc_transport_op* op = grpc_make_transport_op(nullptr);
  ping_result* pr = static_cast<ping_result*>(gpr_malloc(sizeof(*pr)));
  grpc_channel_element* top_elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(channel), 0);
  grpc_core::ExecCtx exec_ctx;
  GPR_ASSERT(reserved == nullptr);
  pr->tag = tag;
  pr->cq = cq;
  GRPC_CLOSURE_INIT(&pr->closure, ping_done, pr, grpc_schedule_on_exec_ctx);
  op->send_ping.on_ack = &pr->closure;
  op->bind_pollset = grpc_cq_pollset(cq);
  GPR_ASSERT(grpc_cq_begin_op(cq, tag));
  top_elem->filter->start_transport_op(top_elem, op);
}
