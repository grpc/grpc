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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include "src/core/ext/census/census_interface.h"
#include "src/core/ext/census/census_rpc_stats.h"
#include "src/core/ext/census/census_tracing.h"
#include "test/core/util/test_config.h"

/* Ensure all possible state transitions are called without causing problem */
static void test_init_shutdown(void) {
  census_stats_store_init();
  census_stats_store_init();
  census_stats_store_shutdown();
  census_stats_store_shutdown();
  census_stats_store_init();
}

static void test_create_and_destroy(void) {
  census_rpc_stats* stats = NULL;
  census_aggregated_rpc_stats agg_stats = {0, NULL};

  stats = census_rpc_stats_create_empty();
  GPR_ASSERT(stats != NULL);
  GPR_ASSERT(stats->cnt == 0 && stats->rpc_error_cnt == 0 &&
             stats->app_error_cnt == 0 && stats->elapsed_time_ms == 0.0 &&
             stats->api_request_bytes == 0 && stats->wire_request_bytes == 0 &&
             stats->api_response_bytes == 0 && stats->wire_response_bytes == 0);
  gpr_free(stats);

  census_aggregated_rpc_stats_set_empty(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 0);
  GPR_ASSERT(agg_stats.stats == NULL);
  agg_stats.num_entries = 1;
  agg_stats.stats = (census_per_method_rpc_stats*)gpr_malloc(
      sizeof(census_per_method_rpc_stats));
  agg_stats.stats[0].method = gpr_strdup("foo");
  census_aggregated_rpc_stats_set_empty(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 0);
  GPR_ASSERT(agg_stats.stats == NULL);
}

#define ASSERT_NEAR(a, b) \
  GPR_ASSERT((a - b) * (a - b) < 1e-24 * (a + b) * (a + b))

static void test_record_and_get_stats(void) {
  census_rpc_stats stats = {1, 2, 3, 4, 5.1, 6.2, 7.3, 8.4};
  census_op_id id;
  census_aggregated_rpc_stats agg_stats = {0, NULL};

  /* Record client stats twice with the same op_id. */
  census_init();
  id = census_tracing_start_op();
  census_add_method_tag(id, "m1");
  census_record_rpc_client_stats(id, &stats);
  census_record_rpc_client_stats(id, &stats);
  census_tracing_end_op(id);
  /* Server stats expect to be empty */
  census_get_server_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 0);
  GPR_ASSERT(agg_stats.stats == NULL);
  /* Client stats expect to have one entry */
  census_get_client_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 1);
  GPR_ASSERT(agg_stats.stats != NULL);
  GPR_ASSERT(strcmp(agg_stats.stats[0].method, "m1") == 0);
  GPR_ASSERT(agg_stats.stats[0].minute_stats.cnt == 2 &&
             agg_stats.stats[0].hour_stats.cnt == 2 &&
             agg_stats.stats[0].total_stats.cnt == 2);
  ASSERT_NEAR(agg_stats.stats[0].minute_stats.wire_response_bytes, 16.8);
  ASSERT_NEAR(agg_stats.stats[0].hour_stats.wire_response_bytes, 16.8);
  ASSERT_NEAR(agg_stats.stats[0].total_stats.wire_response_bytes, 16.8);
  /* Get stats again, results should be the same. */
  census_get_client_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 1);
  census_aggregated_rpc_stats_set_empty(&agg_stats);
  census_shutdown();

  /* Record both server (once) and client (twice) stats with different op_ids.
   */
  census_init();
  id = census_tracing_start_op();
  census_add_method_tag(id, "m2");
  census_record_rpc_client_stats(id, &stats);
  census_tracing_end_op(id);
  id = census_tracing_start_op();
  census_add_method_tag(id, "m3");
  census_record_rpc_server_stats(id, &stats);
  census_tracing_end_op(id);
  id = census_tracing_start_op();
  census_add_method_tag(id, "m4");
  census_record_rpc_client_stats(id, &stats);
  census_tracing_end_op(id);
  /* Check server stats */
  census_get_server_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 1);
  GPR_ASSERT(strcmp(agg_stats.stats[0].method, "m3") == 0);
  GPR_ASSERT(agg_stats.stats[0].minute_stats.app_error_cnt == 3 &&
             agg_stats.stats[0].hour_stats.app_error_cnt == 3 &&
             agg_stats.stats[0].total_stats.app_error_cnt == 3);
  census_aggregated_rpc_stats_set_empty(&agg_stats);
  /* Check client stats */
  census_get_client_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 2);
  GPR_ASSERT(agg_stats.stats != NULL);
  GPR_ASSERT((strcmp(agg_stats.stats[0].method, "m2") == 0 &&
              strcmp(agg_stats.stats[1].method, "m4") == 0) ||
             (strcmp(agg_stats.stats[0].method, "m4") == 0 &&
              strcmp(agg_stats.stats[1].method, "m2") == 0));
  GPR_ASSERT(agg_stats.stats[0].minute_stats.cnt == 1 &&
             agg_stats.stats[1].minute_stats.cnt == 1);
  census_aggregated_rpc_stats_set_empty(&agg_stats);
  census_shutdown();
}

static void test_record_stats_on_unknown_op_id(void) {
  census_op_id unknown_id = {0xDEAD, 0xBEEF};
  census_rpc_stats stats = {1, 2, 3, 4, 5.1, 6.2, 7.3, 8.4};
  census_aggregated_rpc_stats agg_stats = {0, NULL};

  census_init();
  /* Tests that recording stats against unknown id is noop. */
  census_record_rpc_client_stats(unknown_id, &stats);
  census_record_rpc_server_stats(unknown_id, &stats);
  census_get_server_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 0);
  GPR_ASSERT(agg_stats.stats == NULL);
  census_get_client_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 0);
  GPR_ASSERT(agg_stats.stats == NULL);
  census_aggregated_rpc_stats_set_empty(&agg_stats);
  census_shutdown();
}

/* Test that record stats is noop when trace store is uninitialized. */
static void test_record_stats_with_trace_store_uninitialized(void) {
  census_rpc_stats stats = {1, 2, 3, 4, 5.1, 6.2, 7.3, 8.4};
  census_op_id id = {0, 0};
  census_aggregated_rpc_stats agg_stats = {0, NULL};

  census_init();
  id = census_tracing_start_op();
  census_add_method_tag(id, "m");
  census_tracing_end_op(id);
  /* shuts down trace store only. */
  census_tracing_shutdown();
  census_record_rpc_client_stats(id, &stats);
  census_get_client_stats(&agg_stats);
  GPR_ASSERT(agg_stats.num_entries == 0);
  census_stats_store_shutdown();
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_init_shutdown();
  test_create_and_destroy();
  test_record_and_get_stats();
  test_record_stats_on_unknown_op_id();
  test_record_stats_with_trace_store_uninitialized();
  return 0;
}
