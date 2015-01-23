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

#include "src/core/surface/event_string.h"

#include <stdio.h>

#include <grpc/support/string.h>
#include <grpc/byte_buffer.h>

static size_t addhdr(char *p, grpc_event *ev) {
  return sprintf(p, "tag:%p call:%p", ev->tag, (void *)ev->call);
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

static size_t adderr(char *p, grpc_op_error err) {
  return sprintf(p, " err=%s", errstr(err));
}

char *grpc_event_string(grpc_event *ev) {
  char buffer[1024];
  char *p = buffer;

  if (ev == NULL) return gpr_strdup("null");

  switch (ev->type) {
    case GRPC_SERVER_SHUTDOWN:
      p += sprintf(p, "SERVER_SHUTDOWN");
      break;
    case GRPC_QUEUE_SHUTDOWN:
      p += sprintf(p, "QUEUE_SHUTDOWN");
      break;
    case GRPC_READ:
      p += sprintf(p, "READ: ");
      p += addhdr(p, ev);
      if (ev->data.read) {
        p += sprintf(p, " %d bytes",
                     (int)grpc_byte_buffer_length(ev->data.read));
      } else {
        p += sprintf(p, " end-of-stream");
      }
      break;
    case GRPC_INVOKE_ACCEPTED:
      p += sprintf(p, "INVOKE_ACCEPTED: ");
      p += addhdr(p, ev);
      p += adderr(p, ev->data.invoke_accepted);
      break;
    case GRPC_WRITE_ACCEPTED:
      p += sprintf(p, "WRITE_ACCEPTED: ");
      p += addhdr(p, ev);
      p += adderr(p, ev->data.write_accepted);
      break;
    case GRPC_FINISH_ACCEPTED:
      p += sprintf(p, "FINISH_ACCEPTED: ");
      p += addhdr(p, ev);
      p += adderr(p, ev->data.write_accepted);
      break;
    case GRPC_CLIENT_METADATA_READ:
      p += sprintf(p, "CLIENT_METADATA_READ: ");
      p += addhdr(p, ev);
      p += sprintf(p, " %d elements", (int)ev->data.client_metadata_read.count);
      break;
    case GRPC_FINISHED:
      p += sprintf(p, "FINISHED: ");
      p += addhdr(p, ev);
      p += sprintf(p, " status=%d details='%s' %d metadata elements",
                   ev->data.finished.status, ev->data.finished.details,
                   (int)ev->data.finished.metadata_count);
      break;
    case GRPC_SERVER_RPC_NEW:
      p += sprintf(p, "SERVER_RPC_NEW: ");
      p += addhdr(p, ev);
      p += sprintf(p, " method='%s' host='%s' %d metadata elements",
                   ev->data.server_rpc_new.method, ev->data.server_rpc_new.host,
                   (int)ev->data.server_rpc_new.metadata_count);
      break;
    case GRPC_COMPLETION_DO_NOT_USE:
      p += sprintf(p, "DO_NOT_USE (this is a bug)");
      p += addhdr(p, ev);
      break;
  }

  return gpr_strdup(buffer);
}
