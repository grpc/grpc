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

#include <stdarg.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define RETRY_TIMEOUT 300

typedef struct servers_fixture {
  size_t num_servers;
  grpc_server **servers;
  grpc_call **server_calls;
  grpc_completion_queue *cq;
  grpc_completion_queue *shutdown_cq;
  char **servers_hostports;
  grpc_metadata_array *request_metadata_recv;
} servers_fixture;

typedef struct request_sequences {
  size_t n;         /* number of iterations */
  int *connections; /* indexed by the interation number, value is the index of
                       the server it connected to or -1 if none */
  int *connectivity_states; /* indexed by the interation number, value is the
                               client connectivity state */
} request_sequences;

typedef void (*verifier_fn)(const servers_fixture *, grpc_channel *,
                            const request_sequences *, const size_t);

typedef struct test_spec {
  size_t num_iters;
  size_t num_servers;

  int **kill_at;
  int **revive_at;

  const char *description;

  verifier_fn verifier;

} test_spec;

static void test_spec_reset(test_spec *spec) {
  size_t i, j;

  for (i = 0; i < spec->num_iters; i++) {
    for (j = 0; j < spec->num_servers; j++) {
      spec->kill_at[i][j] = 0;
      spec->revive_at[i][j] = 0;
    }
  }
}

static test_spec *test_spec_create(size_t num_iters, size_t num_servers) {
  test_spec *spec;
  size_t i;

  spec = gpr_malloc(sizeof(test_spec));
  spec->num_iters = num_iters;
  spec->num_servers = num_servers;
  spec->kill_at = gpr_malloc(sizeof(int *) * num_iters);
  spec->revive_at = gpr_malloc(sizeof(int *) * num_iters);
  for (i = 0; i < num_iters; i++) {
    spec->kill_at[i] = gpr_malloc(sizeof(int) * num_servers);
    spec->revive_at[i] = gpr_malloc(sizeof(int) * num_servers);
  }

  test_spec_reset(spec);
  return spec;
}

static void test_spec_destroy(test_spec *spec) {
  size_t i;
  for (i = 0; i < spec->num_iters; i++) {
    gpr_free(spec->kill_at[i]);
    gpr_free(spec->revive_at[i]);
  }

  gpr_free(spec->kill_at);
  gpr_free(spec->revive_at);

  gpr_free(spec);
}

static void *tag(intptr_t t) { return (void *)t; }

static gpr_timespec n_millis_time(int n) {
  return gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                      gpr_time_from_millis(n, GPR_TIMESPAN));
}

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, n_millis_time(5000), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void kill_server(const servers_fixture *f, size_t i) {
  gpr_log(GPR_INFO, "KILLING SERVER %" PRIuPTR, i);
  GPR_ASSERT(f->servers[i] != NULL);
  grpc_server_shutdown_and_notify(f->servers[i], f->shutdown_cq, tag(10000));
  GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(10000),
                                         n_millis_time(5000), NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->servers[i]);
  f->servers[i] = NULL;
}

typedef struct request_data {
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_slice details;
  grpc_status_code status;
  grpc_call_details *call_details;
} request_data;

static void revive_server(const servers_fixture *f, request_data *rdata,
                          size_t i) {
  int got_port;
  gpr_log(GPR_INFO, "RAISE AGAIN SERVER %" PRIuPTR, i);
  GPR_ASSERT(f->servers[i] == NULL);

  gpr_log(GPR_DEBUG, "revive: %s", f->servers_hostports[i]);

  f->servers[i] = grpc_server_create(NULL, NULL);
  grpc_server_register_completion_queue(f->servers[i], f->cq, NULL);
  GPR_ASSERT((got_port = grpc_server_add_insecure_http2_port(
                  f->servers[i], f->servers_hostports[i])) > 0);
  grpc_server_start(f->servers[i]);

  GPR_ASSERT(GRPC_CALL_OK ==
             grpc_server_request_call(f->servers[i], &f->server_calls[i],
                                      &rdata->call_details[i],
                                      &f->request_metadata_recv[i], f->cq,
                                      f->cq, tag(1000 + (int)i)));
}

static servers_fixture *setup_servers(const char *server_host,
                                      request_data *rdata,
                                      const size_t num_servers) {
  servers_fixture *f = gpr_malloc(sizeof(servers_fixture));
  size_t i;

  f->num_servers = num_servers;
  f->server_calls = gpr_malloc(sizeof(grpc_call *) * num_servers);
  f->request_metadata_recv =
      gpr_malloc(sizeof(grpc_metadata_array) * num_servers);
  /* Create servers. */
  f->servers = gpr_malloc(sizeof(grpc_server *) * num_servers);
  f->servers_hostports = gpr_malloc(sizeof(char *) * num_servers);
  f->cq = grpc_completion_queue_create_for_next(NULL);
  f->shutdown_cq = grpc_completion_queue_create_for_pluck(NULL);
  for (i = 0; i < num_servers; i++) {
    grpc_metadata_array_init(&f->request_metadata_recv[i]);
    gpr_join_host_port(&f->servers_hostports[i], server_host,
                       grpc_pick_unused_port_or_die());
    f->servers[i] = 0;
    revive_server(f, rdata, i);
  }
  return f;
}

static void teardown_servers(servers_fixture *f) {
  size_t i;
  /* Destroy server. */
  for (i = 0; i < f->num_servers; i++) {
    if (f->servers[i] == NULL) continue;
    grpc_server_shutdown_and_notify(f->servers[i], f->shutdown_cq, tag(10000));
    GPR_ASSERT(grpc_completion_queue_pluck(f->shutdown_cq, tag(10000),
                                           n_millis_time(5000), NULL)
                   .type == GRPC_OP_COMPLETE);
    grpc_server_destroy(f->servers[i]);
  }
  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
  grpc_completion_queue_destroy(f->shutdown_cq);

  gpr_free(f->servers);

  for (i = 0; i < f->num_servers; i++) {
    gpr_free(f->servers_hostports[i]);
    grpc_metadata_array_destroy(&f->request_metadata_recv[i]);
  }

  gpr_free(f->servers_hostports);
  gpr_free(f->request_metadata_recv);
  gpr_free(f->server_calls);
  gpr_free(f);
}

static request_sequences request_sequences_create(size_t n) {
  request_sequences res;
  res.n = n;
  res.connections = gpr_malloc(sizeof(*res.connections) * n);
  res.connectivity_states = gpr_malloc(sizeof(*res.connectivity_states) * n);
  memset(res.connections, 0, sizeof(*res.connections) * n);
  memset(res.connectivity_states, 0, sizeof(*res.connectivity_states) * n);
  return res;
}

static void request_sequences_destroy(const request_sequences *rseqs) {
  gpr_free(rseqs->connections);
  gpr_free(rseqs->connectivity_states);
}

/** Returns connection sequence (server indices), which must be freed */
static request_sequences perform_request(servers_fixture *f,
                                         grpc_channel *client,
                                         request_data *rdata,
                                         const test_spec *spec) {
  grpc_call *c;
  int s_idx;
  int *s_valid;
  grpc_op ops[6];
  grpc_op *op;
  int was_cancelled;
  size_t i, iter_num;
  grpc_event ev;
  int read_tag;
  int completed_client;
  const request_sequences sequences = request_sequences_create(spec->num_iters);

  s_valid = gpr_malloc(sizeof(int) * f->num_servers);

  for (iter_num = 0; iter_num < spec->num_iters; iter_num++) {
    cq_verifier *cqv = cq_verifier_create(f->cq);
    was_cancelled = 2;

    for (i = 0; i < f->num_servers; i++) {
      if (spec->kill_at[iter_num][i] != 0) {
        kill_server(f, i);
      } else if (spec->revive_at[iter_num][i] != 0) {
        /* killing takes precedence */
        revive_server(f, rdata, i);
      }
    }

    sequences.connections[iter_num] = -1;
    grpc_metadata_array_init(&rdata->initial_metadata_recv);
    grpc_metadata_array_init(&rdata->trailing_metadata_recv);

    for (i = 0; i < f->num_servers; i++) {
      grpc_call_details_init(&rdata->call_details[i]);
    }
    memset(s_valid, 0, f->num_servers * sizeof(int));

    grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr");
    c = grpc_channel_create_call(client, NULL, GRPC_PROPAGATE_DEFAULTS, f->cq,
                                 grpc_slice_from_static_string("/foo"), &host,
                                 gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(c);
    completed_client = 0;

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &rdata->initial_metadata_recv;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata =
        &rdata->trailing_metadata_recv;
    op->data.recv_status_on_client.status = &rdata->status;
    op->data.recv_status_on_client.status_details = &rdata->details;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL));

    s_idx = -1;
    while (
        (ev = grpc_completion_queue_next(
             f->cq, grpc_timeout_milliseconds_to_deadline(RETRY_TIMEOUT), NULL))
            .type != GRPC_QUEUE_TIMEOUT) {
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
      read_tag = ((int)(intptr_t)ev.tag);
      const grpc_connectivity_state conn_state =
          grpc_channel_check_connectivity_state(client, 0);
      sequences.connectivity_states[iter_num] = conn_state;
      gpr_log(GPR_DEBUG, "EVENT: success:%d, type:%d, tag:%d iter:%" PRIuPTR,
              ev.success, ev.type, read_tag, iter_num);
      if (ev.success && read_tag >= 1000) {
        GPR_ASSERT(s_idx == -1); /* only one server must reply */
        /* only server notifications for non-shutdown events */
        s_idx = read_tag - 1000;
        s_valid[s_idx] = 1;
        sequences.connections[iter_num] = s_idx;
        break;
      } else if (read_tag == 1) {
        gpr_log(GPR_DEBUG, "client timed out");
        GPR_ASSERT(ev.success);
        completed_client = 1;
      }
    }

    if (s_idx >= 0) {
      memset(ops, 0, sizeof(ops));
      op = ops;
      op->op = GRPC_OP_SEND_INITIAL_METADATA;
      op->data.send_initial_metadata.count = 0;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
      op->data.send_status_from_server.trailing_metadata_count = 0;
      op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
      grpc_slice status_details = grpc_slice_from_static_string("xyz");
      op->data.send_status_from_server.status_details = &status_details;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
      op->data.recv_close_on_server.cancelled = &was_cancelled;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(f->server_calls[s_idx],
                                                       ops, (size_t)(op - ops),
                                                       tag(102), NULL));

      CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
      if (!completed_client) {
        CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
      }
      cq_verify(cqv);

      GPR_ASSERT(rdata->status == GRPC_STATUS_UNIMPLEMENTED);
      GPR_ASSERT(0 == grpc_slice_str_cmp(rdata->details, "xyz"));
      GPR_ASSERT(0 ==
                 grpc_slice_str_cmp(rdata->call_details[s_idx].method, "/foo"));
      GPR_ASSERT(0 == grpc_slice_str_cmp(rdata->call_details[s_idx].host,
                                         "foo.test.google.fr"));
      GPR_ASSERT(was_cancelled == 1);

      grpc_call_unref(f->server_calls[s_idx]);

      /* ask for the next request on this server */
      GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                     f->servers[s_idx], &f->server_calls[s_idx],
                                     &rdata->call_details[s_idx],
                                     &f->request_metadata_recv[s_idx], f->cq,
                                     f->cq, tag(1000 + (int)s_idx)));
    } else { /* no response from server */
      grpc_call_cancel(c, NULL);
      if (!completed_client) {
        CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
        cq_verify(cqv);
      }
    }

    GPR_ASSERT(
        grpc_completion_queue_next(
            f->cq, grpc_timeout_milliseconds_to_deadline(RETRY_TIMEOUT), NULL)
            .type == GRPC_QUEUE_TIMEOUT);

    grpc_metadata_array_destroy(&rdata->initial_metadata_recv);
    grpc_metadata_array_destroy(&rdata->trailing_metadata_recv);

    cq_verifier_destroy(cqv);

    grpc_call_unref(c);

    for (i = 0; i < f->num_servers; i++) {
      grpc_call_details_destroy(&rdata->call_details[i]);
    }
    grpc_slice_unref(rdata->details);
  }

  gpr_free(s_valid);

  return sequences;
}

static grpc_call **perform_multirequest(servers_fixture *f,
                                        grpc_channel *client,
                                        size_t concurrent_calls) {
  grpc_call **calls;
  grpc_op ops[6];
  grpc_op *op;
  size_t i;

  calls = gpr_malloc(sizeof(grpc_call *) * concurrent_calls);
  for (i = 0; i < f->num_servers; i++) {
    kill_server(f, i);
  }

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;

  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr");
  for (i = 0; i < concurrent_calls; i++) {
    calls[i] =
        grpc_channel_create_call(client, NULL, GRPC_PROPAGATE_DEFAULTS, f->cq,
                                 grpc_slice_from_static_string("/foo"), &host,
                                 gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    GPR_ASSERT(calls[i]);
    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(calls[i], ops,
                                                     (size_t)(op - ops), tag(1),
                                                     NULL));
  }

  return calls;
}

void run_spec(const test_spec *spec) {
  grpc_channel *client;
  char *client_hostport;
  char *servers_hostports_str;
  request_data rdata;
  servers_fixture *f;
  grpc_channel_args args;
  grpc_arg arg_array[2];
  rdata.call_details =
      gpr_malloc(sizeof(grpc_call_details) * spec->num_servers);
  f = setup_servers("127.0.0.1", &rdata, spec->num_servers);

  /* Create client. */
  servers_hostports_str = gpr_strjoin_sep((const char **)f->servers_hostports,
                                          f->num_servers, ",", NULL);
  gpr_asprintf(&client_hostport, "ipv4:%s", servers_hostports_str);

  arg_array[0].type = GRPC_ARG_INTEGER;
  arg_array[0].key = "grpc.testing.fixed_reconnect_backoff_ms";
  arg_array[0].value.integer = RETRY_TIMEOUT;
  arg_array[1].type = GRPC_ARG_STRING;
  arg_array[1].key = GRPC_ARG_LB_POLICY_NAME;
  arg_array[1].value.string = "round_robin";
  args.num_args = 2;
  args.args = arg_array;

  client = grpc_insecure_channel_create(client_hostport, &args, NULL);

  gpr_log(GPR_INFO, "Testing '%s' with servers=%s client=%s", spec->description,
          servers_hostports_str, client_hostport);

  const request_sequences sequences = perform_request(f, client, &rdata, spec);

  spec->verifier(f, client, &sequences, spec->num_iters);

  gpr_free(client_hostport);
  gpr_free(servers_hostports_str);
  gpr_free(rdata.call_details);
  request_sequences_destroy(&sequences);

  grpc_channel_destroy(client); /* calls the LB's shutdown func */
  teardown_servers(f);
}

static grpc_channel *create_client(const servers_fixture *f) {
  grpc_channel *client;
  char *client_hostport;
  char *servers_hostports_str;
  grpc_arg arg_array[3];
  grpc_channel_args args;

  servers_hostports_str = gpr_strjoin_sep((const char **)f->servers_hostports,
                                          f->num_servers, ",", NULL);
  gpr_asprintf(&client_hostport, "ipv4:%s", servers_hostports_str);

  arg_array[0].type = GRPC_ARG_INTEGER;
  arg_array[0].key = "grpc.testing.fixed_reconnect_backoff_ms";
  arg_array[0].value.integer = RETRY_TIMEOUT;
  arg_array[1].type = GRPC_ARG_STRING;
  arg_array[1].key = GRPC_ARG_LB_POLICY_NAME;
  arg_array[1].value.string = "ROUND_ROBIN";
  arg_array[2].type = GRPC_ARG_INTEGER;
  arg_array[2].key = GRPC_ARG_HTTP2_MIN_TIME_BETWEEN_PINGS_MS;
  arg_array[2].value.integer = 0;
  args.num_args = GPR_ARRAY_SIZE(arg_array);
  args.args = arg_array;

  client = grpc_insecure_channel_create(client_hostport, &args, NULL);
  gpr_free(client_hostport);
  gpr_free(servers_hostports_str);

  return client;
}

static void test_ping() {
  grpc_channel *client;
  request_data rdata;
  servers_fixture *f;
  cq_verifier *cqv;
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  const size_t num_servers = 1;
  int i;

  rdata.call_details = gpr_malloc(sizeof(grpc_call_details) * num_servers);
  f = setup_servers("127.0.0.1", &rdata, num_servers);
  cqv = cq_verifier_create(f->cq);

  client = create_client(f);

  grpc_channel_ping(client, f->cq, tag(0), NULL);
  CQ_EXPECT_COMPLETION(cqv, tag(0), 0);

  /* check that we're still in idle, and start connecting */
  GPR_ASSERT(grpc_channel_check_connectivity_state(client, 1) ==
             GRPC_CHANNEL_IDLE);
  /* we'll go through some set of transitions (some might be missed), until
     READY is reached */
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        client, state, grpc_timeout_seconds_to_deadline(3), f->cq, tag(99));
    CQ_EXPECT_COMPLETION(cqv, tag(99), 1);
    cq_verify(cqv);
    state = grpc_channel_check_connectivity_state(client, 0);
    GPR_ASSERT(state == GRPC_CHANNEL_READY ||
               state == GRPC_CHANNEL_CONNECTING ||
               state == GRPC_CHANNEL_TRANSIENT_FAILURE);
  }

  for (i = 1; i <= 5; i++) {
    grpc_channel_ping(client, f->cq, tag(i), NULL);
    CQ_EXPECT_COMPLETION(cqv, tag(i), 1);
    cq_verify(cqv);
  }
  gpr_free(rdata.call_details);

  grpc_channel_destroy(client);
  teardown_servers(f);

  cq_verifier_destroy(cqv);
}

static void test_pending_calls(size_t concurrent_calls) {
  size_t i;
  grpc_call **calls;
  grpc_channel *client;
  request_data rdata;
  servers_fixture *f;
  test_spec *spec = test_spec_create(0, 4);
  rdata.call_details =
      gpr_malloc(sizeof(grpc_call_details) * spec->num_servers);
  f = setup_servers("127.0.0.1", &rdata, spec->num_servers);

  client = create_client(f);
  calls = perform_multirequest(f, client, concurrent_calls);
  grpc_call_cancel(
      calls[0],
      NULL); /* exercise the cancel pick path whilst there are pending picks */

  gpr_free(rdata.call_details);

  grpc_channel_destroy(client); /* calls the LB's shutdown func */
  /* destroy the calls after the channel so that they are still around for the
   * LB's shutdown func to process */
  for (i = 0; i < concurrent_calls; i++) {
    grpc_call_unref(calls[i]);
  }
  gpr_free(calls);
  teardown_servers(f);
  test_spec_destroy(spec);
}

static void test_get_channel_info() {
  grpc_channel *channel =
      grpc_insecure_channel_create("ipv4:127.0.0.1:1234", NULL, NULL);
  // Ensures that resolver returns.
  grpc_channel_check_connectivity_state(channel, true /* try_to_connect */);
  // First, request no fields.  This is a no-op.
  grpc_channel_info channel_info;
  memset(&channel_info, 0, sizeof(channel_info));
  grpc_channel_get_info(channel, &channel_info);
  // Request LB policy name.
  char *lb_policy_name = NULL;
  channel_info.lb_policy_name = &lb_policy_name;
  grpc_channel_get_info(channel, &channel_info);
  GPR_ASSERT(lb_policy_name != NULL);
  GPR_ASSERT(strcmp(lb_policy_name, "pick_first") == 0);
  gpr_free(lb_policy_name);
  // Request service config, which does not exist, so we'll get nothing back.
  memset(&channel_info, 0, sizeof(channel_info));
  char *service_config_json = "dummy_string";
  channel_info.service_config_json = &service_config_json;
  grpc_channel_get_info(channel, &channel_info);
  GPR_ASSERT(service_config_json == NULL);
  // Recreate the channel such that it has a service config.
  grpc_channel_destroy(channel);
  grpc_arg arg;
  arg.type = GRPC_ARG_STRING;
  arg.key = GRPC_ARG_SERVICE_CONFIG;
  arg.value.string = "{\"loadBalancingPolicy\": \"ROUND_ROBIN\"}";
  grpc_channel_args *args = grpc_channel_args_copy_and_add(NULL, &arg, 1);
  channel = grpc_insecure_channel_create("ipv4:127.0.0.1:1234", args, NULL);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_args_destroy(&exec_ctx, args);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  // Ensures that resolver returns.
  grpc_channel_check_connectivity_state(channel, true /* try_to_connect */);
  // Now request the service config again.
  grpc_channel_get_info(channel, &channel_info);
  GPR_ASSERT(service_config_json != NULL);
  GPR_ASSERT(strcmp(service_config_json, arg.value.string) == 0);
  gpr_free(service_config_json);
  // Clean up.
  grpc_channel_destroy(channel);
}

static void print_failed_expectations(const int *expected_connection_sequence,
                                      const int *actual_connection_sequence,
                                      const size_t expected_seq_length,
                                      const size_t num_iters) {
  size_t i;
  for (i = 0; i < num_iters; i++) {
    gpr_log(GPR_ERROR,
            "FAILURE: Iter (expected, actual): %" PRIuPTR " (%d, %d)", i,
            expected_connection_sequence[i % expected_seq_length],
            actual_connection_sequence[i]);
  }
}

static void verify_vanilla_round_robin(const servers_fixture *f,
                                       grpc_channel *client,
                                       const request_sequences *sequences,
                                       const size_t num_iters) {
  const size_t expected_seq_length = f->num_servers;

  /* verify conn. seq. expectation */
  /* get the first sequence of "num_servers" elements */
  int *expected_connection_sequence =
      gpr_malloc(sizeof(int) * expected_seq_length);
  memcpy(expected_connection_sequence, sequences->connections,
         sizeof(int) * expected_seq_length);

  for (size_t i = 0; i < num_iters; i++) {
    const int actual = sequences->connections[i];
    const int expected = expected_connection_sequence[i % expected_seq_length];
    if (actual != expected) {
      gpr_log(
          GPR_ERROR,
          "CONNECTION SEQUENCE FAILURE: expected %d, got %d at iteration #%d",
          expected, actual, (int)i);
      abort();
    }
  }

  /* All servers are available, therefore all client subchannels are READY, even
   * when we only need one for the client channel state to be READY */
  for (size_t i = 0; i < sequences->n; i++) {
    const grpc_connectivity_state actual = sequences->connectivity_states[i];
    const grpc_connectivity_state expected = GRPC_CHANNEL_READY;
    if (actual != expected) {
      gpr_log(GPR_ERROR,
              "CONNECTIVITY STATUS SEQUENCE FAILURE: expected '%s', got '%s' "
              "at iteration #%d",
              grpc_connectivity_state_name(expected),
              grpc_connectivity_state_name(actual), (int)i);
      abort();
    }
  }

  gpr_free(expected_connection_sequence);
}

/* At the start of the second iteration, all but the first and last servers (as
 * given in "f") are killed */
static void verify_vanishing_floor_round_robin(
    const servers_fixture *f, grpc_channel *client,
    const request_sequences *sequences, const size_t num_iters) {
  int *expected_connection_sequence;
  const size_t expected_seq_length = 2;
  size_t i;

  /* verify conn. seq. expectation */
  /* copy the first full sequence (without -1s) */
  expected_connection_sequence = gpr_malloc(sizeof(int) * expected_seq_length);
  memcpy(expected_connection_sequence, sequences->connections + 2,
         expected_seq_length * sizeof(int));

  /* first two elements of the sequence should be [0 (1st server), -1 (failure)]
   */
  GPR_ASSERT(sequences->connections[0] == 0);
  GPR_ASSERT(sequences->connections[1] == -1);

  /* the next two element must be [3, 0], repeating from that point: the 3 is
   * brought forth by servers 1 and 2 disappearing after the intial pick of 0 */
  GPR_ASSERT(sequences->connections[2] == 3);
  GPR_ASSERT(sequences->connections[3] == 0);

  /* make sure that the expectation obliges */
  for (i = 2; i < num_iters; i++) {
    const int actual = sequences->connections[i];
    const int expected = expected_connection_sequence[i % expected_seq_length];
    if (actual != expected) {
      print_failed_expectations(expected_connection_sequence,
                                sequences->connections, expected_seq_length,
                                num_iters);
      abort();
    }
  }

  /* There's always at least one subchannel READY (connected), therefore the
   * overall state of the client channel is READY at all times. */
  for (i = 0; i < sequences->n; i++) {
    const grpc_connectivity_state actual = sequences->connectivity_states[i];
    const grpc_connectivity_state expected = GRPC_CHANNEL_READY;
    if (actual != expected) {
      gpr_log(GPR_ERROR,
              "CONNECTIVITY STATUS SEQUENCE FAILURE: expected '%s', got '%s' "
              "at iteration #%d",
              grpc_connectivity_state_name(expected),
              grpc_connectivity_state_name(actual), (int)i);
      abort();
    }
  }

  gpr_free(expected_connection_sequence);
}

static void verify_total_carnage_round_robin(const servers_fixture *f,
                                             grpc_channel *client,
                                             const request_sequences *sequences,
                                             const size_t num_iters) {
  for (size_t i = 0; i < num_iters; i++) {
    const int actual = sequences->connections[i];
    const int expected = -1;
    if (actual != expected) {
      gpr_log(
          GPR_ERROR,
          "CONNECTION SEQUENCE FAILURE: expected %d, got %d at iteration #%d",
          expected, actual, (int)i);
      abort();
    }
  }

  /* No server is ever available. There should be no READY states (or SHUTDOWN).
   * Note that all other states (IDLE, CONNECTING, TRANSIENT_FAILURE) are still
   * possible, as the policy transitions while attempting to reconnect. */
  for (size_t i = 0; i < sequences->n; i++) {
    const grpc_connectivity_state actual = sequences->connectivity_states[i];
    if (actual == GRPC_CHANNEL_READY || actual == GRPC_CHANNEL_SHUTDOWN) {
      gpr_log(GPR_ERROR,
              "CONNECTIVITY STATUS SEQUENCE FAILURE: got unexpected state "
              "'%s' at iteration #%d.",
              grpc_connectivity_state_name(actual), (int)i);
      abort();
    }
  }
}

static void verify_partial_carnage_round_robin(
    const servers_fixture *f, grpc_channel *client,
    const request_sequences *sequences, const size_t num_iters) {
  int *expected_connection_sequence;
  size_t i;
  const size_t expected_seq_length = f->num_servers;

  /* verify conn. seq. expectation */
  /* get the first sequence of "num_servers" elements */
  expected_connection_sequence = gpr_malloc(sizeof(int) * expected_seq_length);
  memcpy(expected_connection_sequence, sequences->connections,
         sizeof(int) * expected_seq_length);

  for (i = 0; i < num_iters / 2; i++) {
    const int actual = sequences->connections[i];
    const int expected = expected_connection_sequence[i % expected_seq_length];
    if (actual != expected) {
      print_failed_expectations(expected_connection_sequence,
                                sequences->connections, expected_seq_length,
                                num_iters);
      abort();
    }
  }

  /* second half of the iterations go without response */
  for (; i < num_iters; i++) {
    GPR_ASSERT(sequences->connections[i] == -1);
  }

  /* We can assert that the first client channel state should be READY, when all
   * servers were available */
  grpc_connectivity_state actual = sequences->connectivity_states[0];
  grpc_connectivity_state expected = GRPC_CHANNEL_READY;
  if (actual != expected) {
    gpr_log(GPR_ERROR,
            "CONNECTIVITY STATUS SEQUENCE FAILURE: expected '%s', got '%s' "
            "at iteration #%d",
            grpc_connectivity_state_name(expected),
            grpc_connectivity_state_name(actual), 0);
    abort();
  }

  /* ... and that the last one shouldn't be READY (or SHUTDOWN): all servers are
   * gone. It may be all other states (IDLE, CONNECTING, TRANSIENT_FAILURE), as
   * the policy transitions while attempting to reconnect. */
  actual = sequences->connectivity_states[num_iters - 1];
  for (i = 0; i < sequences->n; i++) {
    if (actual == GRPC_CHANNEL_READY || actual == GRPC_CHANNEL_SHUTDOWN) {
      gpr_log(GPR_ERROR,
              "CONNECTIVITY STATUS SEQUENCE FAILURE: got unexpected state "
              "'%s' at iteration #%d.",
              grpc_connectivity_state_name(actual), (int)i);
      abort();
    }
  }
  gpr_free(expected_connection_sequence);
}

static void dump_array(const char *desc, const int *data, const size_t count) {
  gpr_strvec s;
  char *tmp;
  size_t i;
  gpr_strvec_init(&s);
  gpr_strvec_add(&s, gpr_strdup(desc));
  gpr_strvec_add(&s, gpr_strdup(":"));
  for (i = 0; i < count; i++) {
    gpr_asprintf(&tmp, " %d", data[i]);
    gpr_strvec_add(&s, tmp);
  }
  tmp = gpr_strvec_flatten(&s, NULL);
  gpr_strvec_destroy(&s);
  gpr_log(GPR_DEBUG, "%s", tmp);
  gpr_free(tmp);
}

static void verify_rebirth_round_robin(const servers_fixture *f,
                                       grpc_channel *client,
                                       const request_sequences *sequences,
                                       const size_t num_iters) {
  dump_array("actual_connection_sequence", sequences->connections, num_iters);

  /* first iteration succeeds */
  GPR_ASSERT(sequences->connections[0] != -1);
  /* then we fail for a while... */
  GPR_ASSERT(sequences->connections[1] == -1);
  /* ... but should be up eventually */
  size_t first_iter_back_up = ~0ul;
  for (size_t i = 2; i < sequences->n; ++i) {
    if (sequences->connections[i] != -1) {
      first_iter_back_up = i;
      break;
    }
  }
  GPR_ASSERT(first_iter_back_up != ~0ul);

  /* We can assert that the first client channel state should be READY, when all
   * servers were available; same thing for the last one. In the middle
   * somewhere there must exist at least one TRANSIENT_FAILURE */
  grpc_connectivity_state actual = sequences->connectivity_states[0];
  grpc_connectivity_state expected = GRPC_CHANNEL_READY;
  if (actual != expected) {
    gpr_log(GPR_ERROR,
            "CONNECTIVITY STATUS SEQUENCE FAILURE: expected '%s', got '%s' "
            "at iteration #%d",
            grpc_connectivity_state_name(expected),
            grpc_connectivity_state_name(actual), 0);
    abort();
  }

  actual = sequences->connectivity_states[num_iters - 1];
  expected = GRPC_CHANNEL_READY;
  if (actual != expected) {
    gpr_log(GPR_ERROR,
            "CONNECTIVITY STATUS SEQUENCE FAILURE: expected '%s', got '%s' "
            "at iteration #%d",
            grpc_connectivity_state_name(expected),
            grpc_connectivity_state_name(actual), (int)num_iters - 1);
    abort();
  }

  bool found_failure_status = false;
  for (size_t i = 1; i < sequences->n - 1; i++) {
    if (sequences->connectivity_states[i] == GRPC_CHANNEL_TRANSIENT_FAILURE) {
      found_failure_status = true;
      break;
    }
  }
  if (!found_failure_status) {
    gpr_log(
        GPR_ERROR,
        "CONNECTIVITY STATUS SEQUENCE FAILURE: "
        "GRPC_CHANNEL_TRANSIENT_FAILURE status not found. Got the following "
        "instead:");
    for (size_t i = 0; i < num_iters; i++) {
      gpr_log(GPR_ERROR, "[%d]: %s", (int)i,
              grpc_connectivity_state_name(sequences->connectivity_states[i]));
    }
  }
}

int main(int argc, char **argv) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  test_spec *spec;
  size_t i;
  const size_t NUM_ITERS = 10;
  const size_t NUM_SERVERS = 4;

  grpc_init();
  grpc_test_init(argc, argv);
  grpc_tracer_set_enabled("round_robin", 1);

  GPR_ASSERT(grpc_lb_policy_create(&exec_ctx, "this-lb-policy-does-not-exist",
                                   NULL) == NULL);
  GPR_ASSERT(grpc_lb_policy_create(&exec_ctx, NULL, NULL) == NULL);

  spec = test_spec_create(NUM_ITERS, NUM_SERVERS);
  /* everything is fine, all servers stay up the whole time and life's peachy
   */
  spec->verifier = verify_vanilla_round_robin;
  spec->description = "test_all_server_up";
  run_spec(spec);

  /* Kill all servers first thing in the morning */
  test_spec_reset(spec);
  spec->verifier = verify_total_carnage_round_robin;
  spec->description = "test_kill_all_server";
  for (i = 0; i < NUM_SERVERS; i++) {
    spec->kill_at[0][i] = 1;
  }
  run_spec(spec);

  /* at the start of the 2nd iteration, kill all but the first and last
   * servers.
   * This should knock down the server bound to be selected next */
  test_spec_reset(spec);
  spec->verifier = verify_vanishing_floor_round_robin;
  spec->description = "test_kill_middle_servers_at_2nd_iteration";
  for (i = 1; i < NUM_SERVERS - 1; i++) {
    spec->kill_at[1][i] = 1;
  }
  run_spec(spec);

  /* Midway, kill all servers. */
  test_spec_reset(spec);
  spec->verifier = verify_partial_carnage_round_robin;
  spec->description = "test_kill_all_server_midway";
  for (i = 0; i < NUM_SERVERS; i++) {
    spec->kill_at[spec->num_iters / 2][i] = 1;
  }
  run_spec(spec);

  /* After first iteration, kill all servers. On the third one, bring them all
   * back up. */
  test_spec_reset(spec);
  spec->verifier = verify_rebirth_round_robin;
  spec->description = "test_kill_all_server_after_1st_resurrect_at_3rd";
  for (i = 0; i < NUM_SERVERS; i++) {
    spec->kill_at[1][i] = 1;
    spec->revive_at[3][i] = 1;
  }
  run_spec(spec);
  test_spec_destroy(spec);

  test_pending_calls(4);
  test_ping();
  test_get_channel_info();

  grpc_exec_ctx_finish(&exec_ctx);
  grpc_shutdown();
  return 0;
}
