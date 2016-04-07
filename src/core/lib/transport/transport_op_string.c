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

#include "src/core/lib/channel/channel_stack.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/lib/support/string.h"

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
  if (gpr_time_cmp(md.deadline, gpr_inf_future(md.deadline.clock_type)) != 0) {
    char *tmp;
    gpr_asprintf(&tmp, " deadline=%lld.%09d", (long long)md.deadline.tv_sec,
                 (int)md.deadline.tv_nsec);
    gpr_strvec_add(b, tmp);
  }
}

char *grpc_transport_stream_op_string(grpc_transport_stream_op *op) {
  char *tmp;
  char *out;
  int first = 1;

  gpr_strvec b;
  gpr_strvec_init(&b);

  if (op->send_initial_metadata != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("SEND_INITIAL_METADATA{"));
    put_metadata_list(&b, *op->send_initial_metadata);
    gpr_strvec_add(&b, gpr_strdup("}"));
  }

  if (op->send_message != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_asprintf(&tmp, "SEND_MESSAGE:flags=0x%08x:len=%d",
                 op->send_message->flags, op->send_message->length);
    gpr_strvec_add(&b, tmp);
  }

  if (op->send_trailing_metadata != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("SEND_TRAILING_METADATA{"));
    put_metadata_list(&b, *op->send_trailing_metadata);
    gpr_strvec_add(&b, gpr_strdup("}"));
  }

  if (op->recv_initial_metadata != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("RECV_INITIAL_METADATA"));
  }

  if (op->recv_message != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("RECV_MESSAGE"));
  }

  if (op->recv_trailing_metadata != NULL) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_strvec_add(&b, gpr_strdup("RECV_TRAILING_METADATA"));
  }

  if (op->cancel_with_status != GRPC_STATUS_OK) {
    if (!first) gpr_strvec_add(&b, gpr_strdup(" "));
    first = 0;
    gpr_asprintf(&tmp, "CANCEL:%d", op->cancel_with_status);
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
