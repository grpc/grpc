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
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/ext/census/census_interface.h"
#include "src/core/ext/census/census_rpc_stats.h"
#include "test/core/util/test_config.h"

/* Tests census noop stubs in a simulated rpc flow */
void test_census_stubs(void) {
  census_op_id op_id;
  census_rpc_stats *stats = census_rpc_stats_create_empty();
  census_aggregated_rpc_stats data_map = {0, NULL};

  /* Initializes census library at server start up time. */
  census_init();
  /* Starts tracing at the beginning of a rpc. */
  op_id = census_tracing_start_op();
  /* Appends custom annotations on a trace object. */
  census_tracing_print(op_id, "annotation foo");
  census_tracing_print(op_id, "annotation bar");
  /* Appends method tag on the trace object. */
  census_add_method_tag(op_id, "service_foo/method.bar");
  /* Either record client side stats or server side stats associated with the
     op_id. Here for testing purpose, we record both. */
  census_record_rpc_client_stats(op_id, stats);
  census_record_rpc_server_stats(op_id, stats);
  /* Ends a tracing. */
  census_tracing_end_op(op_id);
  /* In process stats queries. */
  census_get_server_stats(&data_map);
  census_aggregated_rpc_stats_set_empty(&data_map);
  census_get_client_stats(&data_map);
  census_aggregated_rpc_stats_set_empty(&data_map);
  gpr_free(stats);
  census_shutdown();
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_census_stubs();
  return 0;
}
