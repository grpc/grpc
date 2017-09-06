/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
