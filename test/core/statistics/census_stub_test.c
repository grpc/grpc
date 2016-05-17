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
