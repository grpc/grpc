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
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

/* These routines are here to facilitate debugging - they produce string
   representations of various transport data structures */

static void put_metadata(gpr_strvec *b, grpc_mdelem *md) {
  gpr_strvec_add(b, gpr_strdup("key="));
  gpr_strvec_add(b,
                 gpr_dump_slice(md->key->slice, GPR_DUMP_HEX | GPR_DUMP_ASCII));

  gpr_strvec_add(b, gpr_strdup(" value="));
  gpr_strvec_add(
      b, gpr_dump_slice(md->value->slice, GPR_DUMP_HEX | GPR_DUMP_ASCII));
}

static void put_metadata_list(gpr_strvec *b, grpc_metadata_batch md) {
  grpc_linked_mdelem *m;
  for (m = md.list.head; m != NULL; m = m->next) {
    if (m != md.list.head) gpr_strvec_add(b, gpr_strdup(", "));
    put_metadata(b, m->md);
  }
  if (gpr_time_cmp(md.deadline, gpr_inf_future) != 0) {
    char *tmp;
    gpr_asprintf(&tmp, " deadline=%d.%09d", md.deadline.tv_sec,
                 md.deadline.tv_nsec);
    gpr_strvec_add(b, tmp);
  }
}

char *grpc_sopb_string(grpc_stream_op_buffer *sopb) {
  char *out;
  char *tmp;
  size_t i;
  gpr_strvec b;
  gpr_strvec_init(&b);

  for (i = 0; i < sopb->nops; i++) {
    grpc_stream_op *op = &sopb->ops[i];
    if (i > 0) gpr_strvec_add(&b, gpr_strdup(", "));
    switch (op->type) {
      case GRPC_NO_OP:
        gpr_strvec_add(&b, gpr_strdup("NO_OP"));
        break;
      case GRPC_OP_BEGIN_MESSAGE:
        gpr_asprintf(&tmp, "BEGIN_MESSAGE:%d", op->data.begin_message.length);
        gpr_strvec_add(&b, tmp);
        break;
      case GRPC_OP_SLICE:
        gpr_asprintf(&tmp, "SLICE:%d", GPR_SLICE_LENGTH(op->data.slice));
        gpr_strvec_add(&b, tmp);
        break;
      case GRPC_OP_METADATA:
        gpr_strvec_add(&b, gpr_strdup("METADATA{"));
        put_metadata_list(&b, op->data.metadata);
        gpr_strvec_add(&b, gpr_strdup("}"));
        break;
    }
  }

  out = gpr_strvec_flatten(&b, NULL);
  gpr_strvec_destroy(&b);

  return out;
}

char *grpc_transport_stream_op_string(grpc_transport_stream_op *op) {
  char *tmp;
  char *out;
  int first = 1;

  gpr_strvec b;
  gpr_strvec_init(&b);

  if (op->send_ops) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("SEND"));
    if (op->is_last_send) {
      gpr_strvec_add(&b, gpr_strdup("_LAST"));
    }
    gpr_strvec_add(&b, gpr_strdup("["));
    gpr_strvec_add(&b, grpc_sopb_string(op->send_ops));
    gpr_strvec_add(&b, gpr_strdup("]"));
  }

  if (op->recv_ops) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_asprintf(&tmp, "RECV:max_recv_bytes=%d", op->max_recv_bytes);
    gpr_strvec_add(&b, tmp);
  }

  if (op->bind_pollset) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("BIND"));
  }

  if (op->cancel_with_status != GRPC_STATUS_OK) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_asprintf(&tmp, "CANCEL:%d", op->cancel_with_status);
    gpr_strvec_add(&b, tmp);
  }

  if (op->on_consumed != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_asprintf(&tmp, "ON_CONSUMED:%p", op->on_consumed);
    gpr_strvec_add(&b, tmp);
  }

  out = gpr_strvec_flatten(&b, NULL);
  gpr_strvec_destroy(&b);

  return out;
}

void grpc_call_log_op(char *file, int line, gpr_log_severity severity,
                      grpc_call_element *elem, grpc_transport_stream_op *op) {
  char *str = grpc_transport_stream_op_string(op);
  gpr_log(file, line, severity, "OP[%s:%p]: %s", elem->filter->name, elem, str);
  gpr_free(str);
}
