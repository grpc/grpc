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

#include "src/core/channel/channel_stack.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/useful.h>

static void put_metadata(gpr_strvec *b, grpc_mdelem *md) {
  gpr_strvec_add(b, gpr_strdup(" key="));
  gpr_strvec_add(b, gpr_hexdump((char *)GPR_SLICE_START_PTR(md->key->slice),
                    GPR_SLICE_LENGTH(md->key->slice), GPR_HEXDUMP_PLAINTEXT));

  gpr_strvec_add(b, gpr_strdup(" value="));
  gpr_strvec_add(b, gpr_hexdump((char *)GPR_SLICE_START_PTR(md->value->slice),
                    GPR_SLICE_LENGTH(md->value->slice), GPR_HEXDUMP_PLAINTEXT));
}

char *grpc_call_op_string(grpc_call_op *op) {
  char *tmp;
  char *out;

  gpr_strvec b;
  gpr_strvec_init(&b);

  switch (op->dir) {
    case GRPC_CALL_DOWN:
      gpr_strvec_add(&b, gpr_strdup(">"));
      break;
    case GRPC_CALL_UP:
      gpr_strvec_add(&b, gpr_strdup("<"));
      break;
  }
  switch (op->type) {
    case GRPC_SEND_METADATA:
      gpr_strvec_add(&b, gpr_strdup("SEND_METADATA"));
      put_metadata(&b, op->data.metadata);
      break;
    case GRPC_SEND_DEADLINE:
      gpr_asprintf(&tmp, "SEND_DEADLINE %d.%09d", op->data.deadline.tv_sec,
              op->data.deadline.tv_nsec);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_SEND_START:
      gpr_asprintf(&tmp, "SEND_START pollset=%p", op->data.start.pollset);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_SEND_MESSAGE:
      gpr_strvec_add(&b, gpr_strdup("SEND_MESSAGE"));
      break;
    case GRPC_SEND_PREFORMATTED_MESSAGE:
      gpr_strvec_add(&b, gpr_strdup("SEND_PREFORMATTED_MESSAGE"));
      break;
    case GRPC_SEND_FINISH:
      gpr_strvec_add(&b, gpr_strdup("SEND_FINISH"));
      break;
    case GRPC_REQUEST_DATA:
      gpr_strvec_add(&b, gpr_strdup("REQUEST_DATA"));
      break;
    case GRPC_RECV_METADATA:
      gpr_strvec_add(&b, gpr_strdup("RECV_METADATA"));
      put_metadata(&b, op->data.metadata);
      break;
    case GRPC_RECV_DEADLINE:
      gpr_asprintf(&tmp, "RECV_DEADLINE %d.%09d", op->data.deadline.tv_sec,
              op->data.deadline.tv_nsec);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_RECV_END_OF_INITIAL_METADATA:
      gpr_strvec_add(&b, gpr_strdup("RECV_END_OF_INITIAL_METADATA"));
      break;
    case GRPC_RECV_MESSAGE:
      gpr_strvec_add(&b, gpr_strdup("RECV_MESSAGE"));
      break;
    case GRPC_RECV_HALF_CLOSE:
      gpr_strvec_add(&b, gpr_strdup("RECV_HALF_CLOSE"));
      break;
    case GRPC_RECV_FINISH:
      gpr_strvec_add(&b, gpr_strdup("RECV_FINISH"));
      break;
    case GRPC_CANCEL_OP:
      gpr_strvec_add(&b, gpr_strdup("CANCEL_OP"));
      break;
  }
  gpr_asprintf(&tmp, " flags=0x%08x", op->flags);
  gpr_strvec_add(&b, tmp);

  out = gpr_strvec_flatten(&b, NULL);
  gpr_strvec_destroy(&b);

  return out;
}

void grpc_call_log_op(char *file, int line, gpr_log_severity severity,
                      grpc_call_element *elem, grpc_call_op *op) {
  char *str = grpc_call_op_string(op);
  gpr_log(file, line, severity, "OP[%s:%p]: %s", elem->filter->name, elem, str);
  gpr_free(str);
}
