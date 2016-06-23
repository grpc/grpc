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

/* Test of gpr thread local storage support. */

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/tls.h>
#include <stdio.h>
#include <stdlib.h>
#include "test/core/util/test_config.h"

#define NUM_THREADS 100

GPR_TLS_DECL(test_var);

static void thd_body(void *arg) {
  intptr_t i;

  GPR_ASSERT(gpr_tls_get(&test_var) == 0);

  for (i = 0; i < 100000; i++) {
    gpr_tls_set(&test_var, i);
    GPR_ASSERT(gpr_tls_get(&test_var) == i);
  }
  gpr_tls_set(&test_var, 0);
}

/* ------------------------------------------------- */

int main(int argc, char *argv[]) {
  gpr_thd_options opt = gpr_thd_options_default();
  int i;
  gpr_thd_id threads[NUM_THREADS];

  grpc_test_init(argc, argv);

  gpr_tls_init(&test_var);

  gpr_thd_options_set_joinable(&opt);

  for (i = 0; i < NUM_THREADS; i++) {
    gpr_thd_new(&threads[i], thd_body, NULL, &opt);
  }
  for (i = 0; i < NUM_THREADS; i++) {
    gpr_thd_join(threads[i]);
  }

  gpr_tls_destroy(&test_var);

  return 0;
}
