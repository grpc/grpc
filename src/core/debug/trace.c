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

#include "src/core/debug/trace.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/support/env.h"

#if GRPC_ENABLE_TRACING
gpr_uint32 grpc_trace_bits = 0;

static void add(const char *beg, const char *end, char ***ss, size_t *ns) {
  size_t n = *ns;
  size_t np = n + 1;
  char *s = gpr_malloc(end - beg + 1);
  memcpy(s, beg, end - beg);
  s[end-beg] = 0;
  *ss = gpr_realloc(*ss, sizeof(char**) * np);
  (*ss)[n] = s;
  *ns = np;
}

static void split(const char *s, char ***ss, size_t *ns) {
  const char *c = strchr(s, ',');
  if (c == NULL) {
    add(s, s + strlen(s), ss, ns);
  } else {
    add(s, c, ss, ns);
    split(c+1, ss, ns);
  }
}

static void parse(const char *s) {
  char **strings = NULL;
  size_t nstrings = 0;
  size_t i;
  split(s, &strings, &nstrings);

  grpc_trace_bits = 0;

  for (i = 0; i < nstrings; i++) {
    const char *s = strings[i];
    if (0 == strcmp(s, "surface")) {
      grpc_trace_bits |= GRPC_TRACE_SURFACE;
    } else if (0 == strcmp(s, "channel")) {
      grpc_trace_bits |= GRPC_TRACE_CHANNEL;
    } else if (0 == strcmp(s, "tcp")) {
      grpc_trace_bits |= GRPC_TRACE_TCP;
    } else if (0 == strcmp(s, "secure_endpoint")) {
      grpc_trace_bits |= GRPC_TRACE_SECURE_ENDPOINT;
    } else if (0 == strcmp(s, "http")) {
      grpc_trace_bits |= GRPC_TRACE_HTTP;
    } else if (0 == strcmp(s, "ssl")) {
      grpc_trace_bits |= GRPC_TRACE_SSL;
    } else if (0 == strcmp(s, "all")) {
      grpc_trace_bits = -1;
    } else {
      gpr_log(GPR_ERROR, "Unknown trace var: '%s'", s);
    }
  }

  for (i = 0; i < nstrings; i++) {
    gpr_free(strings[i]);
  }
  gpr_free(strings);
}

void grpc_init_trace_bits() {
  char *e = gpr_getenv("GRPC_TRACE");
  if (e == NULL) {
    grpc_trace_bits = 0;
  } else {
    parse(e);
    gpr_free(e);
  }
}
#else
void grpc_init_trace_bits() {
}
#endif

