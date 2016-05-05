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

#include "test/core/end2end/end2end_tests.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "src/core/ext/client_config/client_channel.h"
#include "src/core/ext/load_reporting/load_reporting.h"
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static grpc_load_reporting_config *g_client_lrc;
static grpc_load_reporting_config *g_server_lrc;

typedef struct fullstack_fixture_data {
  char *localaddr;
} fullstack_fixture_data;

static grpc_end2end_test_fixture chttp2_create_fixture_fullstack(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  int port = grpc_pick_unused_port_or_die();
  fullstack_fixture_data *ffd = gpr_malloc(sizeof(fullstack_fixture_data));
  memset(&f, 0, sizeof(f));

  gpr_join_host_port(&ffd->localaddr, "localhost", port);

  f.fixture_data = ffd;
  f.cq = grpc_completion_queue_create(NULL);

  return f;
}

typedef struct {
  int64_t total_bytes;
  bool fully_processed;
  uint32_t initial_token;
  uint32_t final_token;
} aggregated_bw_stats;

static void sample_fn(const grpc_load_reporting_call_data *call_data,
                      void *user_data) {
  GPR_ASSERT(user_data != NULL);
  aggregated_bw_stats *custom_stats = (aggregated_bw_stats *)user_data;
  if (call_data == NULL) {
    /* initial invocation */
    custom_stats->initial_token = 0xDEADBEEF;
  } else {
    /* final invocation */
    custom_stats->total_bytes =
        (int64_t)(call_data->stats->transport_stream_stats.outgoing.data_bytes +
                  call_data->stats->transport_stream_stats.incoming.data_bytes);
    custom_stats->final_token = 0xCAFED00D;
    custom_stats->fully_processed = true;
  }
}

void chttp2_init_client_fullstack(grpc_end2end_test_fixture *f,
                                  grpc_channel_args *client_args) {
  fullstack_fixture_data *ffd = f->fixture_data;
  grpc_arg arg = grpc_load_reporting_config_create_arg(g_client_lrc);
  client_args = grpc_channel_args_copy_and_add(client_args, &arg, 1);
  f->client = grpc_insecure_channel_create(ffd->localaddr, client_args, NULL);
  grpc_channel_args_destroy(client_args);
  GPR_ASSERT(f->client);
}

void chttp2_init_server_fullstack(grpc_end2end_test_fixture *f,
                                  grpc_channel_args *server_args) {
  fullstack_fixture_data *ffd = f->fixture_data;
  if (f->server) {
    grpc_server_destroy(f->server);
  }
  grpc_arg arg = grpc_load_reporting_config_create_arg(g_server_lrc);
  server_args = grpc_channel_args_copy_and_add(server_args, &arg, 1);
  f->server = grpc_server_create(server_args, NULL);
  grpc_channel_args_destroy(server_args);
  grpc_server_register_completion_queue(f->server, f->cq, NULL);
  GPR_ASSERT(grpc_server_add_insecure_http2_port(f->server, ffd->localaddr));
  grpc_server_start(f->server);
}

void chttp2_tear_down_fullstack(grpc_end2end_test_fixture *f) {
  fullstack_fixture_data *ffd = f->fixture_data;
  gpr_free(ffd->localaddr);
  gpr_free(ffd);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/fullstack+loadreporting", FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION,
     chttp2_create_fixture_fullstack, chttp2_init_client_fullstack,
     chttp2_init_server_fullstack, chttp2_tear_down_fullstack},
};

int main(int argc, char **argv) {
  size_t i;

  aggregated_bw_stats *aggr_stats_client =
      gpr_malloc(sizeof(aggregated_bw_stats));
  aggr_stats_client->total_bytes = -1;
  aggr_stats_client->fully_processed = false;
  aggregated_bw_stats *aggr_stats_server =
      gpr_malloc(sizeof(aggregated_bw_stats));
  aggr_stats_server->total_bytes = -1;
  aggr_stats_server->fully_processed = false;

  g_client_lrc =
      grpc_load_reporting_config_create(sample_fn, aggr_stats_client);
  g_server_lrc =
      grpc_load_reporting_config_create(sample_fn, aggr_stats_server);

  grpc_test_init(argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  grpc_load_reporting_config_destroy(g_client_lrc);
  grpc_load_reporting_config_destroy(g_server_lrc);

  if (aggr_stats_client->fully_processed) {
    GPR_ASSERT(aggr_stats_client->total_bytes >= 0);
    GPR_ASSERT(aggr_stats_client->initial_token == 0xDEADBEEF);
    GPR_ASSERT(aggr_stats_client->final_token == 0xCAFED00D);
  }
  if (aggr_stats_server->fully_processed) {
    GPR_ASSERT(aggr_stats_server->total_bytes >= 0);
    GPR_ASSERT(aggr_stats_server->initial_token == 0xDEADBEEF);
    GPR_ASSERT(aggr_stats_server->final_token == 0xCAFED00D);
  }

  gpr_free(aggr_stats_client);
  gpr_free(aggr_stats_server);

  return 0;
}
