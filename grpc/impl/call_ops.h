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


#ifndef TEST_GRPC_C_CALL_OPS_H
#define TEST_GRPC_C_CALL_OPS_H

#include "../grpc_c_public.h"
#include <grpc/grpc.h>
#include <stdbool.h>

typedef void (*grpc_op_filler)(grpc_op *op, const grpc_method *, grpc_context *, const grpc_message message, void *response);
typedef void (*grpc_op_finisher)(grpc_context *, bool *status, int max_message_size);

typedef struct grpc_op_manager {
  const grpc_op_filler fill;
  const grpc_op_finisher finish;
} grpc_op_manager;

enum { GRPC_MAX_OP_COUNT = 8 };

typedef const grpc_op_manager grpc_call_set[GRPC_MAX_OP_COUNT];

void grpc_fill_op_from_call_set(grpc_call_set set, const grpc_method *rpc_method, grpc_context *context,
                                const grpc_message message, void *response, grpc_op ops[], size_t *nops);

void grpc_finish_op_from_call_set(grpc_call_set set, grpc_context *context);

/* list of operations */

extern const grpc_op_manager grpc_op_send_metadata;
extern const grpc_op_manager grpc_op_recv_metadata;
extern const grpc_op_manager grpc_op_send_object;
extern const grpc_op_manager grpc_op_recv_object;
extern const grpc_op_manager grpc_op_send_close;
extern const grpc_op_manager grpc_op_recv_status;

#endif //TEST_GRPC_C_CALL_OPS_H
