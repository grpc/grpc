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

#include "src/core/surface/call.h"

#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

static void add_metadata(gpr_strvec *b, const grpc_metadata *md, size_t count) {
  size_t i;
  for (i = 0; i < count; i++) {
    gpr_strvec_add(b, gpr_strdup("\nkey="));
    gpr_strvec_add(b, gpr_strdup(md[i].key));

    gpr_strvec_add(b, gpr_strdup(" value="));
    gpr_strvec_add(b, gpr_dump(md[i].value, md[i].value_length,
                               GPR_DUMP_HEX | GPR_DUMP_ASCII));
  }
}

char *grpc_op_string(const grpc_op *op) {
  char *tmp;
  char *out;

  gpr_strvec b;
  gpr_strvec_init(&b);

  switch (op->op) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      gpr_strvec_add(&b, gpr_strdup("SEND_INITIAL_METADATA"));
      add_metadata(&b, op->data.send_initial_metadata.metadata,
                   op->data.send_initial_metadata.count);
      break;
    case GRPC_OP_SEND_MESSAGE:
      gpr_asprintf(&tmp, "SEND_MESSAGE ptr=%p", op->data.send_message);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      gpr_strvec_add(&b, gpr_strdup("SEND_CLOSE_FROM_CLIENT"));
      break;
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      gpr_asprintf(&tmp, "SEND_STATUS_FROM_SERVER status=%d details=%s",
                   op->data.send_status_from_server.status,
                   op->data.send_status_from_server.status_details);
      gpr_strvec_add(&b, tmp);
      add_metadata(&b, op->data.send_status_from_server.trailing_metadata,
                   op->data.send_status_from_server.trailing_metadata_count);
      break;
    case GRPC_OP_RECV_INITIAL_METADATA:
      gpr_asprintf(&tmp, "RECV_INITIAL_METADATA ptr=%p",
                   op->data.recv_initial_metadata);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_RECV_MESSAGE:
      gpr_asprintf(&tmp, "RECV_MESSAGE ptr=%p", op->data.recv_message);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      gpr_asprintf(&tmp,
                   "RECV_STATUS_ON_CLIENT metadata=%p status=%p details=%p",
                   op->data.recv_status_on_client.trailing_metadata,
                   op->data.recv_status_on_client.status,
                   op->data.recv_status_on_client.status_details);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      gpr_asprintf(&tmp, "RECV_CLOSE_ON_SERVER cancelled=%p",
                   op->data.recv_close_on_server.cancelled);
      gpr_strvec_add(&b, tmp);
  }
  out = gpr_strvec_flatten(&b, NULL);
  gpr_strvec_destroy(&b);

  return out;
}

void grpc_call_log_batch(char *file, int line, gpr_log_severity severity,
                         grpc_call *call, const grpc_op *ops, size_t nops,
                         void *tag) {
  char *tmp;
  size_t i;
  for (i = 0; i < nops; i++) {
    tmp = grpc_op_string(&ops[i]);
    gpr_log(file, line, severity, "ops[%d]: %s", i, tmp);
    gpr_free(tmp);
  }
}
