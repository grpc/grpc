/*
 *
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

#include "src/core/client_config/subchannel.h"

#include <grpc/support/alloc.h>

struct grpc_subchannel {
  gpr_refcount refs;
};

struct grpc_subchannel_call {
  grpc_subchannel *subchannel;
  gpr_refcount refs;
};

#define SUBCHANNEL_CALL_TO_CALL_STACK(call) (((grpc_call_stack *)(call)) + 1)

/*
 * grpc_subchannel implementation
 */

void grpc_subchannel_ref(grpc_subchannel *channel) { gpr_ref(&channel->refs); }

void grpc_subchannel_unref(grpc_subchannel *channel) {
  if (gpr_unref(&channel->refs)) {
    gpr_free(channel);
  }
}

/*
 * grpc_subchannel_call implementation
 */

void grpc_subchannel_call_ref(grpc_subchannel_call *call) {
  gpr_ref(&call->refs);
}

void grpc_subchannel_call_unref(grpc_subchannel_call *call) {
  if (gpr_unref(&call->refs)) {
    grpc_call_stack_destroy(SUBCHANNEL_CALL_TO_CALL_STACK(call));
    grpc_subchannel_unref(call->subchannel);
    gpr_free(call);
  }
}

void grpc_subchannel_call_process_op(grpc_subchannel_call *call,
                                     grpc_transport_stream_op *op) {
  grpc_call_stack *call_stack = SUBCHANNEL_CALL_TO_CALL_STACK(call);
  grpc_call_element *top_elem = grpc_call_stack_element(call_stack, 0);
  top_elem->filter->start_transport_stream_op(top_elem, op);
}
