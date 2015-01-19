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

#include "src/core/channel/channel_stack.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string.h>
#include <grpc/support/useful.h>

#define MAX_APPEND 1024

typedef struct {
  size_t cap;
  size_t len;
  char *buffer;
} buf;

static void bprintf(buf *b, const char *fmt, ...) {
  va_list arg;
  if (b->len + MAX_APPEND > b->cap) {
    b->cap = GPR_MAX(b->len + MAX_APPEND, b->cap * 3 / 2);
    b->buffer = gpr_realloc(b->buffer, b->cap);
  }
  va_start(arg, fmt);
  b->len += vsprintf(b->buffer + b->len, fmt, arg);
  va_end(arg);
}

static void bputs(buf *b, const char *s) {
  size_t slen = strlen(s);
  if (b->len + slen + 1 > b->cap) {
    b->cap = GPR_MAX(b->len + slen + 1, b->cap * 3 / 2);
    b->buffer = gpr_realloc(b->buffer, b->cap);
  }
  strcat(b->buffer, s);
  b->len += slen;
}

static void put_metadata(buf *b, grpc_mdelem *md) {
  char *txt;

  txt = gpr_hexdump((char *)GPR_SLICE_START_PTR(md->key->slice),
                    GPR_SLICE_LENGTH(md->key->slice), GPR_HEXDUMP_PLAINTEXT);
  bputs(b, " key=");
  bputs(b, txt);
  gpr_free(txt);

  txt = gpr_hexdump((char *)GPR_SLICE_START_PTR(md->value->slice),
                    GPR_SLICE_LENGTH(md->value->slice), GPR_HEXDUMP_PLAINTEXT);
  bputs(b, " value=");
  bputs(b, txt);
  gpr_free(txt);
}

char *grpc_call_op_string(grpc_call_op *op) {
  buf b = {0, 0, 0};

  switch (op->dir) {
    case GRPC_CALL_DOWN:
      bprintf(&b, ">");
      break;
    case GRPC_CALL_UP:
      bprintf(&b, "<");
      break;
  }
  switch (op->type) {
    case GRPC_SEND_METADATA:
      bprintf(&b, "SEND_METADATA");
      put_metadata(&b, op->data.metadata);
      break;
    case GRPC_SEND_DEADLINE:
      bprintf(&b, "SEND_DEADLINE %d.%09d", op->data.deadline.tv_sec,
              op->data.deadline.tv_nsec);
      break;
    case GRPC_SEND_START:
      bprintf(&b, "SEND_START pollset=%p", op->data.start.pollset);
      break;
    case GRPC_SEND_MESSAGE:
      bprintf(&b, "SEND_MESSAGE");
      break;
    case GRPC_SEND_PREFORMATTED_MESSAGE:
      bprintf(&b, "SEND_PREFORMATTED_MESSAGE");
      break;
    case GRPC_SEND_FINISH:
      bprintf(&b, "SEND_FINISH");
      break;
    case GRPC_REQUEST_DATA:
      bprintf(&b, "REQUEST_DATA");
      break;
    case GRPC_RECV_METADATA:
      bprintf(&b, "RECV_METADATA");
      put_metadata(&b, op->data.metadata);
      break;
    case GRPC_RECV_DEADLINE:
      bprintf(&b, "RECV_DEADLINE %d.%09d", op->data.deadline.tv_sec,
              op->data.deadline.tv_nsec);
      break;
    case GRPC_RECV_END_OF_INITIAL_METADATA:
      bprintf(&b, "RECV_END_OF_INITIAL_METADATA");
      break;
    case GRPC_RECV_MESSAGE:
      bprintf(&b, "RECV_MESSAGE");
      break;
    case GRPC_RECV_HALF_CLOSE:
      bprintf(&b, "RECV_HALF_CLOSE");
      break;
    case GRPC_RECV_FINISH:
      bprintf(&b, "RECV_FINISH");
      break;
    case GRPC_CANCEL_OP:
      bprintf(&b, "CANCEL_OP");
      break;
  }
  bprintf(&b, " flags=0x%08x", op->flags);

  return b.buffer;
}

void grpc_call_log_op(char *file, int line, gpr_log_severity severity,
                      grpc_call_element *elem, grpc_call_op *op) {
  char *str = grpc_call_op_string(op);
  gpr_log(file, line, severity, "OP[%s:%p]: %s", elem->filter->name, elem, str);
  gpr_free(str);
}
