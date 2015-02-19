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

#include "src/core/surface/event_string.h"

#include <stdio.h>

#include "src/core/support/string.h"
#include <grpc/byte_buffer.h>

static void addhdr(gpr_strvec *buf, grpc_event *ev) {
  char *tmp;
  gpr_asprintf(&tmp, "tag:%p call:%p", ev->tag, (void *)ev->call);
  gpr_strvec_add(buf, tmp);
}

static const char *errstr(grpc_op_error err) {
  switch (err) {
    case GRPC_OP_OK:
      return "OK";
    case GRPC_OP_ERROR:
      return "ERROR";
  }
  return "UNKNOWN_UNKNOWN";
}

static void adderr(gpr_strvec *buf, grpc_op_error err) {
  char *tmp;
  gpr_asprintf(&tmp, " err=%s", errstr(err));
  gpr_strvec_add(buf, tmp);
}

char *grpc_event_string(grpc_event *ev) {
  char *out;
  char *tmp;
  gpr_strvec buf;

  if (ev == NULL) return gpr_strdup("null");

  gpr_strvec_init(&buf);

  switch (ev->type) {
    case GRPC_SERVER_SHUTDOWN:
      gpr_strvec_add(&buf, gpr_strdup("SERVER_SHUTDOWN"));
      break;
    case GRPC_QUEUE_SHUTDOWN:
      gpr_strvec_add(&buf, gpr_strdup("QUEUE_SHUTDOWN"));
      break;
    case GRPC_READ:
      gpr_strvec_add(&buf, gpr_strdup("READ: "));
      addhdr(&buf, ev);
      if (ev->data.read) {
        gpr_asprintf(&tmp, " %d bytes",
                     (int)grpc_byte_buffer_length(ev->data.read));
        gpr_strvec_add(&buf, tmp);
      } else {
        gpr_strvec_add(&buf, gpr_strdup(" end-of-stream"));
      }
      break;
    case GRPC_OP_COMPLETE:
      gpr_strvec_add(&buf, gpr_strdup("OP_COMPLETE: "));
      addhdr(&buf, ev);
      adderr(&buf, ev->data.op_complete);
      break;
    case GRPC_WRITE_ACCEPTED:
      gpr_strvec_add(&buf, gpr_strdup("WRITE_ACCEPTED: "));
      addhdr(&buf, ev);
      adderr(&buf, ev->data.write_accepted);
      break;
    case GRPC_FINISH_ACCEPTED:
      gpr_strvec_add(&buf, gpr_strdup("FINISH_ACCEPTED: "));
      addhdr(&buf, ev);
      adderr(&buf, ev->data.write_accepted);
      break;
    case GRPC_CLIENT_METADATA_READ:
      gpr_strvec_add(&buf, gpr_strdup("CLIENT_METADATA_READ: "));
      addhdr(&buf, ev);
      gpr_asprintf(&tmp, " %d elements",
                   (int)ev->data.client_metadata_read.count);
      gpr_strvec_add(&buf, tmp);
      break;
    case GRPC_FINISHED:
      gpr_strvec_add(&buf, gpr_strdup("FINISHED: "));
      addhdr(&buf, ev);
      gpr_asprintf(&tmp, " status=%d details='%s' %d metadata elements",
                   ev->data.finished.status, ev->data.finished.details,
                   (int)ev->data.finished.metadata_count);
      gpr_strvec_add(&buf, tmp);
      break;
    case GRPC_SERVER_RPC_NEW:
      gpr_strvec_add(&buf, gpr_strdup("SERVER_RPC_NEW: "));
      addhdr(&buf, ev);
      gpr_asprintf(&tmp, " method='%s' host='%s' %d metadata elements",
                   ev->data.server_rpc_new.method, ev->data.server_rpc_new.host,
                   (int)ev->data.server_rpc_new.metadata_count);
      gpr_strvec_add(&buf, tmp);
      break;
    case GRPC_COMPLETION_DO_NOT_USE:
      gpr_strvec_add(&buf, gpr_strdup("DO_NOT_USE (this is a bug)"));
      addhdr(&buf, ev);
      break;
  }

  out = gpr_strvec_flatten(&buf, NULL);
  gpr_strvec_destroy(&buf);
  return out;
}
