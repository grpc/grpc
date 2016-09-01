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

#include "src/core/lib/surface/event_string.h"

#include <stdio.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/support/string.h"

static void addhdr(gpr_strvec *buf, grpc_event *ev) {
  char *tmp;
  gpr_asprintf(&tmp, "tag:%p", ev->tag);
  gpr_strvec_add(buf, tmp);
}

static const char *errstr(int success) { return success ? "OK" : "ERROR"; }

static void adderr(gpr_strvec *buf, int success) {
  char *tmp;
  gpr_asprintf(&tmp, " %s", errstr(success));
  gpr_strvec_add(buf, tmp);
}

char *grpc_event_string(grpc_event *ev) {
  char *out;
  gpr_strvec buf;

  if (ev == NULL) return gpr_strdup("null");

  gpr_strvec_init(&buf);

  switch (ev->type) {
    case GRPC_QUEUE_TIMEOUT:
      gpr_strvec_add(&buf, gpr_strdup("QUEUE_TIMEOUT"));
      break;
    case GRPC_QUEUE_SHUTDOWN:
      gpr_strvec_add(&buf, gpr_strdup("QUEUE_SHUTDOWN"));
      break;
    case GRPC_OP_COMPLETE:
      gpr_strvec_add(&buf, gpr_strdup("OP_COMPLETE: "));
      addhdr(&buf, ev);
      adderr(&buf, ev->success);
      break;
  }

  out = gpr_strvec_flatten(&buf, NULL);
  gpr_strvec_destroy(&buf);
  return out;
}
