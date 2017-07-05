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

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/ext/census/census_interface.h"
#include "src/core/ext/census/census_tracing.h"
#include "src/core/ext/census/census_tracing.h"
#include "test/core/util/test_config.h"

/* Ensure all possible state transitions are called without causing problem */
static void test_init_shutdown(void) {
  census_tracing_init();
  census_tracing_init();
  census_tracing_shutdown();
  census_tracing_shutdown();
  census_tracing_init();
}

static void test_start_op_generates_locally_unique_ids(void) {
/* Check that ids generated within window size of 1000 are unique.
   TODO(hongyu): Replace O(n^2) duplicate detection algorithm with O(nlogn)
   algorithm. Enhance the test to larger window size (>10^6) */
#define WINDOW_SIZE 1000
  census_op_id ids[WINDOW_SIZE];
  int i;
  census_init();
  for (i = 0; i < WINDOW_SIZE; i++) {
    ids[i] = census_tracing_start_op();
    census_tracing_end_op(ids[i]);
  }
  for (i = 0; i < WINDOW_SIZE - 1; i++) {
    int j;
    for (j = i + 1; j < WINDOW_SIZE; j++) {
      GPR_ASSERT(ids[i].upper != ids[j].upper || ids[i].lower != ids[j].lower);
    }
  }
#undef WINDOW_SIZE
  census_shutdown();
}

static void test_get_trace_method_name(void) {
  census_op_id id;
  const char write_name[] = "service/method";
  census_tracing_init();
  id = census_tracing_start_op();
  census_add_method_tag(id, write_name);
  census_internal_lock_trace_store();
  {
    const char *read_name =
        census_get_trace_method_name(census_get_trace_obj_locked(id));
    GPR_ASSERT(strcmp(read_name, write_name) == 0);
  }
  census_internal_unlock_trace_store();
  census_tracing_shutdown();
}

typedef struct thd_arg {
  int num_done;
  gpr_cv done;
  gpr_mu mu;
} thd_arg;

static void mimic_trace_op_sequences(void *arg) {
  census_op_id id;
  const char *method_name = "service_foo/method_bar";
  int i = 0;
  const int num_iter = 200;
  thd_arg *args = (thd_arg *)arg;
  GPR_ASSERT(args != NULL);
  gpr_log(GPR_INFO, "Start trace op sequence thread.");
  for (i = 0; i < num_iter; i++) {
    id = census_tracing_start_op();
    census_add_method_tag(id, method_name);
    /* pretend doing 1us work. */
    gpr_sleep_until(GRPC_TIMEOUT_MICROS_TO_DEADLINE(1));
    census_tracing_end_op(id);
  }
  gpr_log(GPR_INFO, "End trace op sequence thread.");
  gpr_mu_lock(&args->mu);
  args->num_done += 1;
  gpr_cv_broadcast(&args->done);
  gpr_mu_unlock(&args->mu);
}

static void test_concurrency(void) {
#define NUM_THREADS 1000
  gpr_thd_id tid[NUM_THREADS];
  int i = 0;
  thd_arg arg;
  arg.num_done = 0;
  gpr_mu_init(&arg.mu);
  gpr_cv_init(&arg.done);
  census_tracing_init();
  for (i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_new(tid + i, mimic_trace_op_sequences, &arg, NULL);
  }
  gpr_mu_lock(&arg.mu);
  while (arg.num_done < NUM_THREADS) {
    gpr_log(GPR_INFO, "num done %d", arg.num_done);
    gpr_cv_wait(&arg.done, &arg.mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  gpr_mu_unlock(&arg.mu);
  census_tracing_shutdown();
#undef NUM_THREADS
}

static void test_add_method_tag_to_unknown_op_id(void) {
  census_op_id unknown_id = {0xDEAD, 0xBEEF};
  int ret = 0;
  census_tracing_init();
  ret = census_add_method_tag(unknown_id, "foo");
  GPR_ASSERT(ret != 0);
  census_tracing_shutdown();
}

static void test_trace_print(void) {
  census_op_id id;
  int i;
  const char *annotation_txt[4] = {"abc", "", "$%^ *()_"};
  char long_txt[CENSUS_MAX_ANNOTATION_LENGTH + 10];

  memset(long_txt, 'a', GPR_ARRAY_SIZE(long_txt));
  long_txt[CENSUS_MAX_ANNOTATION_LENGTH + 9] = '\0';
  annotation_txt[3] = long_txt;

  census_tracing_init();
  id = census_tracing_start_op();
  /* Adds large number of annotations to each trace */
  for (i = 0; i < 1000; i++) {
    census_tracing_print(id,
                         annotation_txt[i % GPR_ARRAY_SIZE(annotation_txt)]);
  }
  census_tracing_end_op(id);

  census_tracing_shutdown();
}

/* Returns 1 if two ids are equal, otherwise returns 0. */
static int ids_equal(census_op_id id1, census_op_id id2) {
  return (id1.upper == id2.upper) && (id1.lower == id2.lower);
}

static void test_get_active_ops(void) {
  census_op_id id_1, id_2, id_3;
  census_trace_obj **active_ops;
  const char *annotation_txt[] = {"annotation 1", "a2"};
  int i = 0;
  int n = 0;

  gpr_log(GPR_INFO, "test_get_active_ops");
  census_tracing_init();
  /* No active ops before calling start_op(). */
  active_ops = census_get_active_ops(&n);
  GPR_ASSERT(active_ops == NULL);
  GPR_ASSERT(n == 0);

  /* Starts one op */
  id_1 = census_tracing_start_op();
  census_add_method_tag(id_1, "foo_1");
  active_ops = census_get_active_ops(&n);
  GPR_ASSERT(active_ops != NULL);
  GPR_ASSERT(n == 1);
  GPR_ASSERT(ids_equal(active_ops[0]->id, id_1));
  census_trace_obj_destroy(active_ops[0]);
  gpr_free(active_ops);
  active_ops = NULL;

  /* Start the second and the third ops */
  id_2 = census_tracing_start_op();
  census_add_method_tag(id_2, "foo_2");
  id_3 = census_tracing_start_op();
  census_add_method_tag(id_3, "foo_3");

  active_ops = census_get_active_ops(&n);
  GPR_ASSERT(n == 3);
  for (i = 0; i < 3; i++) {
    census_trace_obj_destroy(active_ops[i]);
  }
  gpr_free(active_ops);
  active_ops = NULL;

  /* End the second op  and add annotations to the third ops */
  census_tracing_end_op(id_2);
  census_tracing_print(id_3, annotation_txt[0]);
  census_tracing_print(id_3, annotation_txt[1]);

  active_ops = census_get_active_ops(&n);
  GPR_ASSERT(active_ops != NULL);
  GPR_ASSERT(n == 2);
  for (i = 0; i < 2; i++) {
    census_trace_obj_destroy(active_ops[i]);
  }
  gpr_free(active_ops);
  active_ops = NULL;

  /* End all ops. */
  census_tracing_end_op(id_1);
  census_tracing_end_op(id_3);
  active_ops = census_get_active_ops(&n);
  GPR_ASSERT(active_ops == NULL);
  GPR_ASSERT(n == 0);

  census_tracing_shutdown();
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_init_shutdown();
  test_start_op_generates_locally_unique_ids();
  test_get_trace_method_name();
  test_concurrency();
  test_add_method_tag_to_unknown_op_id();
  test_trace_print();
  test_get_active_ops();
  return 0;
}
