/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>

#include "src/core/lib/load_reporting/load_reporting.h"

typedef struct load_reporting {
  gpr_mu mu;
  load_reporting_fn fn;
  void *data;
} load_reporting;

static load_reporting g_load_reporting;

void grpc_load_reporting_init(load_reporting_fn fn, void *data) {
  gpr_mu_init(&g_load_reporting.mu);
  g_load_reporting.fn = fn;
  g_load_reporting.data = data;
}

void grpc_load_reporting_destroy() {
  gpr_free(g_load_reporting.data);
  g_load_reporting.data = NULL;
  gpr_mu_destroy(&g_load_reporting.mu);
}

void grpc_load_reporting_call(const grpc_call_stats *stats) {
  if (g_load_reporting.fn != NULL) {
    gpr_mu_lock(&g_load_reporting.mu);
    g_load_reporting.fn(g_load_reporting.data, stats);
    gpr_mu_unlock(&g_load_reporting.mu);
  }
}

void *grpc_load_reporting_data() {
  gpr_mu_lock(&g_load_reporting.mu);
  void *data = g_load_reporting.data;
  gpr_mu_unlock(&g_load_reporting.mu);
  return data;
}
