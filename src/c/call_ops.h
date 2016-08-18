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

#ifndef GRPC_C_INTERNAL_CALL_OPS_H
#define GRPC_C_INTERNAL_CALL_OPS_H

#include <grpc/grpc.h>
#include <grpc_c/codegen/method.h>
#include <grpc_c/grpc_c.h>
#include <stdbool.h>
#include "src/c/context.h"
#include "src/c/message.h"

typedef struct GRPC_call_op_set GRPC_call_op_set;

typedef bool (*GRPC_op_filler)(grpc_op *op, GRPC_context *, GRPC_call_op_set *,
                               const grpc_message message, void *response);
typedef void (*GRPC_op_finisher)(GRPC_context *, GRPC_call_op_set *,
                                 bool *status, int max_message_size);

typedef struct GRPC_op_manager {
  const GRPC_op_filler fill;
  const GRPC_op_finisher finish;
} GRPC_op_manager;

enum { GRPC_MAX_OP_COUNT = 8 };

typedef struct GRPC_closure {
  void *arg;
  void (*callback)(void *arg);
} GRPC_closure;

struct GRPC_call_op_set {
  const GRPC_op_manager operations[GRPC_MAX_OP_COUNT];
  GRPC_context *const context;

  /*
   * These are used to work with completion queue.
   */
  /* if this is true (default false), the event tagged by this call_op_set will
   * not be emitted
   * from the completion queue wrapper. */
  bool hide_from_user;

  // used in async calls
  void *user_tag;
  bool *user_done;            /* for clients reading a stream */
  GRPC_closure async_cleanup; /* will be called when the op_set finishes */
                              /* used to cleanup after RPC */

  /*
   * these are used by individual operations.
   * don't initialize them by hand
   */
  /* pointer to the user-supplied object which shall receive deserialized data
   */
  void *received_object;
  grpc_byte_buffer *recv_buffer;
  /* Holding onto the buffer to free it later */
  grpc_byte_buffer *send_buffer;
  bool message_received;
};

size_t GRPC_fill_op_from_call_set(GRPC_call_op_set *set, GRPC_context *context,
                                  const grpc_message message, void *response,
                                  grpc_op *ops, size_t *nops);

/* Runs post processing steps in the call op set. Returns false if something
 * wrong happens e.g. serialization. */
bool GRPC_finish_op_from_call_set(GRPC_call_op_set *set, GRPC_context *context);

void GRPC_start_batch_from_op_set(grpc_call *call, GRPC_call_op_set *set,
                                  GRPC_context *context,
                                  const grpc_message message, void *response);

/* list of operations */

extern const GRPC_op_manager grpc_op_send_metadata;
extern const GRPC_op_manager grpc_op_recv_metadata;
extern const GRPC_op_manager grpc_op_send_object;
extern const GRPC_op_manager grpc_op_recv_object;
extern const GRPC_op_manager grpc_op_client_send_close;
extern const GRPC_op_manager grpc_op_client_recv_status;
extern const GRPC_op_manager grpc_op_server_recv_close;
extern const GRPC_op_manager grpc_op_server_send_status;
extern const GRPC_op_manager grpc_op_server_decode_context_payload;

#endif /* GRPC_C_INTERNAL_CALL_OPS_H */
