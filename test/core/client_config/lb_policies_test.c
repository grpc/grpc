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

#include <stdarg.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/string_util.h>

#include "src/core/channel/channel_stack.h"
#include "src/core/surface/channel.h"
#include "src/core/channel/client_channel.h"
#include "src/core/support/string.h"
#include "src/core/surface/server.h"
#include "test/core/util/test_config.h"
#include "test/core/util/port.h"
#include "test/core/end2end/cq_verifier.h"

typedef struct servers_fixture {
  size_t num_servers;
  grpc_server **servers;
  grpc_call **server_calls;
  grpc_completion_queue *cq;
  char **servers_hostports;
  grpc_metadata_array *request_metadata_recv;
} servers_fixture;

typedef void (*verifier_fn)(const servers_fixture *, grpc_channel *,
                            const int *, const size_t);

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

static void *tag(gpr_intptr t) { return (void *)t; }

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
  gpr_log(GPR_INFO, "KILLING SERVER %d", i);
  GPR_ASSERT(f->servers[i] != NULL);
  grpc_server_shutdown_and_notify(f->servers[i], f->cq, tag(10000));
  GPR_ASSERT(
      grpc_completion_queue_pluck(f->cq, tag(10000), n_millis_time(5000), NULL)
          .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->servers[i]);
  f->servers[i] = NULL;
}

typedef struct request_data {
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  char *details;
  size_t details_capacity;
  grpc_status_code status;
  grpc_call_details *call_details;
} request_data;

static void revive_server(const servers_fixture *f, request_data *rdata,
                          size_t i) {
  int got_port;
  gpr_log(GPR_INFO, "RAISE AGAIN SERVER %d", i);
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
  f->cq = grpc_completion_queue_create(NULL);
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
    grpc_server_shutdown_and_notify(f->servers[i], f->cq, tag(10000));
    GPR_ASSERT(grpc_completion_queue_pluck(f->cq, tag(10000),
                                           n_millis_time(5000), NULL)
                   .type == GRPC_OP_COMPLETE);
    grpc_server_destroy(f->servers[i]);
  }
  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);

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

/** Returns connection sequence (server indices), which must be freed */
int *perform_request(servers_fixture *f, grpc_channel *client,
                     request_data *rdata, const test_spec *spec) {
  grpc_call *c;
  int s_idx;
  int *s_valid;
  grpc_op ops[6];
  grpc_op *op;
  int was_cancelled;
  size_t i, iter_num;
  grpc_event ev;
  int read_tag;
  int *connection_sequence;
  int completed_client;

  s_valid = gpr_malloc(sizeof(int) * f->num_servers);
  connection_sequence = gpr_malloc(sizeof(int) * spec->num_iters);

  /* Send a trivial request. */

  for (iter_num = 0; iter_num < spec->num_iters; iter_num++) {
    cq_verifier *cqv = cq_verifier_create(f->cq);
    rdata->details = NULL;
    rdata->details_capacity = 0;
    was_cancelled = 2;

    for (i = 0; i < f->num_servers; i++) {
      if (spec->kill_at[iter_num][i] != 0) {
        kill_server(f, i);
      } else if (spec->revive_at[iter_num][i] != 0) {
        /* killing takes precedence */
        revive_server(f, rdata, i);
      }
    }

    connection_sequence[iter_num] = -1;
    grpc_metadata_array_init(&rdata->initial_metadata_recv);
    grpc_metadata_array_init(&rdata->trailing_metadata_recv);

    for (i = 0; i < f->num_servers; i++) {
      grpc_call_details_init(&rdata->call_details[i]);
    }
    memset(s_valid, 0, f->num_servers * sizeof(int));

    c = grpc_channel_create_call(client, NULL, GRPC_PROPAGATE_DEFAULTS, f->cq,
                                 "/foo", "foo.test.google.fr", gpr_inf_future(GPR_CLOCK_REALTIME),
                                 NULL);
    GPR_ASSERT(c);
    completed_client = 0;

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
    op->data.recv_initial_metadata = &rdata->initial_metadata_recv;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata =
        &rdata->trailing_metadata_recv;
    op->data.recv_status_on_client.status = &rdata->status;
    op->data.recv_status_on_client.status_details = &rdata->details;
    op->data.recv_status_on_client.status_details_capacity =
        &rdata->details_capacity;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL));

    s_idx = -1;
    while ((ev = grpc_completion_queue_next(f->cq, n_millis_time(s_idx == -1 ? 3000 : 200), NULL))
               .type != GRPC_QUEUE_TIMEOUT) {
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
      read_tag = ((int)(gpr_intptr)ev.tag);
      gpr_log(GPR_DEBUG, "EVENT: success:%d, type:%d, tag:%d iter:%d",
              ev.success, ev.type, read_tag, iter_num);
      if (ev.success && read_tag >= 1000) {
        GPR_ASSERT(s_idx == -1); /* only one server must reply */
        /* only server notifications for non-shutdown events */
        s_idx = read_tag - 1000;
        s_valid[s_idx] = 1;
        connection_sequence[iter_num] = s_idx;
      } else if (read_tag == 1) {
        gpr_log(GPR_DEBUG, "client timed out");
        GPR_ASSERT(ev.success);
        completed_client = 1;
      }
    }

    gpr_log(GPR_DEBUG, "s_idx=%d", s_idx);

    if (s_idx >= 0) {
      op = ops;
      op->op = GRPC_OP_SEND_INITIAL_METADATA;
      op->data.send_initial_metadata.count = 0;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
      op->data.send_status_from_server.trailing_metadata_count = 0;
      op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
      op->data.send_status_from_server.status_details = "xyz";
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

      cq_expect_completion(cqv, tag(102), 1);
      if (!completed_client) {
        cq_expect_completion(cqv, tag(1), 1);
      }
      cq_verify(cqv);

      gpr_log(GPR_DEBUG, "status=%d; %s", rdata->status, rdata->details);
      GPR_ASSERT(rdata->status == GRPC_STATUS_UNIMPLEMENTED);
      GPR_ASSERT(0 == strcmp(rdata->details, "xyz"));
      GPR_ASSERT(0 == strcmp(rdata->call_details[s_idx].method, "/foo"));
      GPR_ASSERT(0 ==
                 strcmp(rdata->call_details[s_idx].host, "foo.test.google.fr"));
      GPR_ASSERT(was_cancelled == 1);

      grpc_call_destroy(f->server_calls[s_idx]);

      /* ask for the next request on this server */
      GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                     f->servers[s_idx], &f->server_calls[s_idx],
                                     &rdata->call_details[s_idx],
                                     &f->request_metadata_recv[s_idx], f->cq,
                                     f->cq, tag(1000 + (int)s_idx)));
    } else {
      grpc_call_cancel(c, NULL);
      if (!completed_client) {
        cq_expect_completion(cqv, tag(1), 1);
        cq_verify(cqv);
      }
    }

    grpc_metadata_array_destroy(&rdata->initial_metadata_recv);
    grpc_metadata_array_destroy(&rdata->trailing_metadata_recv);

    cq_verifier_destroy(cqv);

    grpc_call_destroy(c);

    for (i = 0; i < f->num_servers; i++) {
      grpc_call_details_destroy(&rdata->call_details[i]);
    }
    gpr_free(rdata->details);
  }

  gpr_free(s_valid);

  return connection_sequence;
}

static void assert_channel_connectivity(
    grpc_channel *ch, size_t num_accepted_conn_states,
    grpc_connectivity_state accepted_conn_state, ...) {
  size_t i;
  grpc_channel_stack *client_stack;
  grpc_channel_element *client_channel_filter;
  grpc_connectivity_state actual_conn_state;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  va_list ap;

  client_stack = grpc_channel_get_channel_stack(ch);
  client_channel_filter = grpc_channel_stack_last_element(client_stack);

  actual_conn_state = grpc_client_channel_check_connectivity_state(
      &exec_ctx, client_channel_filter, 0 /* don't try to connect */);
  grpc_exec_ctx_finish(&exec_ctx);
  va_start(ap, accepted_conn_state);
  for (i = 0; i < num_accepted_conn_states; i++) {
    if (actual_conn_state == accepted_conn_state) {
      break;
    }
    accepted_conn_state = va_arg(ap, grpc_connectivity_state);
  }
  va_end(ap);
  if (i == num_accepted_conn_states) {
    char **accepted_strs =
        gpr_malloc(sizeof(char *) * num_accepted_conn_states);
    char *accepted_str_joined;
    va_start(ap, accepted_conn_state);
    for (i = 0; i < num_accepted_conn_states; i++) {
      GPR_ASSERT(gpr_asprintf(&accepted_strs[i], "%d", accepted_conn_state) >
                 0);
      accepted_conn_state = va_arg(ap, grpc_connectivity_state);
    }
    va_end(ap);
    accepted_str_joined = gpr_strjoin_sep((const char **)accepted_strs,
                                          num_accepted_conn_states, ", ", NULL);
    gpr_log(
        GPR_ERROR,
        "Channel connectivity assertion failed: expected <one of [%s]>, got %d",
        accepted_str_joined, actual_conn_state);

    for (i = 0; i < num_accepted_conn_states; i++) {
      gpr_free(accepted_strs[i]);
    }
    gpr_free(accepted_strs);
    gpr_free(accepted_str_joined);
    abort();
  }
}

void run_spec(const test_spec *spec) {
  grpc_channel *client;
  char *client_hostport;
  char *servers_hostports_str;
  int *actual_connection_sequence;
  request_data rdata;
  servers_fixture *f;
  rdata.call_details =
      gpr_malloc(sizeof(grpc_call_details) * spec->num_servers);
  f = setup_servers("127.0.0.1", &rdata, spec->num_servers);

  /* Create client. */
  servers_hostports_str = gpr_strjoin_sep((const char **)f->servers_hostports,
                                          f->num_servers, ",", NULL);
  gpr_asprintf(&client_hostport, "ipv4:%s?lb_policy=round_robin",
               servers_hostports_str);
  client = grpc_insecure_channel_create(client_hostport, NULL, NULL);

  gpr_log(GPR_INFO, "Testing '%s' with servers=%s client=%s", spec->description,
          servers_hostports_str, client_hostport);

  actual_connection_sequence = perform_request(f, client, &rdata, spec);

  spec->verifier(f, client, actual_connection_sequence, spec->num_iters);

  gpr_free(client_hostport);
  gpr_free(servers_hostports_str);
  gpr_free(actual_connection_sequence);
  gpr_free(rdata.call_details);

  grpc_channel_destroy(client);
  teardown_servers(f);
}

static void print_failed_expectations(const int *expected_connection_sequence,
                                      const int *actual_connection_sequence,
                                      const size_t expected_seq_length,
                                      const size_t num_iters) {
  size_t i;
  for (i = 0; i < num_iters; i++) {
    gpr_log(GPR_ERROR, "FAILURE: Iter, expected, actual:%d (%d, %d)", i,
            expected_connection_sequence[i % expected_seq_length],
            actual_connection_sequence[i]);
  }
}

static void verify_vanilla_round_robin(const servers_fixture *f,
                                       grpc_channel *client,
                                       const int *actual_connection_sequence,
                                       const size_t num_iters) {
  int *expected_connection_sequence;
  size_t i;
  const size_t expected_seq_length = f->num_servers;

  /* verify conn. seq. expectation */
  /* get the first sequence of "num_servers" elements */
  expected_connection_sequence = gpr_malloc(sizeof(int) * expected_seq_length);
  memcpy(expected_connection_sequence, actual_connection_sequence,
         sizeof(int) * expected_seq_length);

  for (i = 0; i < num_iters; i++) {
    const int actual = actual_connection_sequence[i];
    const int expected = expected_connection_sequence[i % expected_seq_length];
    if (actual != expected) {
      gpr_log(GPR_ERROR, "FAILURE: expected %d, actual %d at iter %d", expected,
              actual, i);
      print_failed_expectations(expected_connection_sequence,
                                actual_connection_sequence, expected_seq_length,
                                num_iters);
      abort();
    }
  }
  assert_channel_connectivity(client, 1, GRPC_CHANNEL_READY);

  gpr_free(expected_connection_sequence);
}

/* At the start of the second iteration, all but the first and last servers (as
 * given in "f") are killed */
static void verify_vanishing_floor_round_robin(
    const servers_fixture *f, grpc_channel *client,
    const int *actual_connection_sequence, const size_t num_iters) {
  int *expected_connection_sequence;
  const size_t expected_seq_length = 2;
  size_t i;

  /* verify conn. seq. expectation */
  /* copy the first full sequence (without -1s) */
  expected_connection_sequence = gpr_malloc(sizeof(int) * expected_seq_length);
  memcpy(expected_connection_sequence, actual_connection_sequence + 2,
         expected_seq_length * sizeof(int));

  /* first three elements of the sequence should be [<1st>, -1] */
  if (actual_connection_sequence[0] != expected_connection_sequence[0]) {
    gpr_log(GPR_ERROR, "FAILURE: expected %d, actual %d at iter %d",
            expected_connection_sequence[0], actual_connection_sequence[0], 0);
    print_failed_expectations(expected_connection_sequence,
                              actual_connection_sequence, expected_seq_length,
                              1u);
    abort();
  }

  GPR_ASSERT(actual_connection_sequence[1] == -1);

  for (i = 2; i < num_iters; i++) {
    const int actual = actual_connection_sequence[i];
    const int expected = expected_connection_sequence[i % expected_seq_length];
    if (actual != expected) {
      gpr_log(GPR_ERROR, "FAILURE: expected %d, actual %d at iter %d", expected,
              actual, i);
      print_failed_expectations(expected_connection_sequence,
                                actual_connection_sequence, expected_seq_length,
                                num_iters);
      abort();
    }
  }
  gpr_free(expected_connection_sequence);
}

static void verify_total_carnage_round_robin(
    const servers_fixture *f, grpc_channel *client,
    const int *actual_connection_sequence, const size_t num_iters) {
  size_t i;

  for (i = 0; i < num_iters; i++) {
    const int actual = actual_connection_sequence[i];
    const int expected = -1;
    if (actual != expected) {
      gpr_log(GPR_ERROR, "FAILURE: expected %d, actual %d at iter %d", expected,
              actual, i);
      abort();
    }
  }

  /* even though we know all the servers are dead, the client is still trying
   * retrying, believing it's in a transient failure situation */
  assert_channel_connectivity(client, 2, GRPC_CHANNEL_TRANSIENT_FAILURE,
                              GRPC_CHANNEL_CONNECTING);
}

static void verify_partial_carnage_round_robin(
    const servers_fixture *f, grpc_channel *client,
    const int *actual_connection_sequence, const size_t num_iters) {
  int *expected_connection_sequence;
  size_t i;
  const size_t expected_seq_length = f->num_servers;

  /* verify conn. seq. expectation */
  /* get the first sequence of "num_servers" elements */
  expected_connection_sequence = gpr_malloc(sizeof(int) * expected_seq_length);
  memcpy(expected_connection_sequence, actual_connection_sequence,
         sizeof(int) * expected_seq_length);

  for (i = 0; i < num_iters / 2; i++) {
    const int actual = actual_connection_sequence[i];
    const int expected = expected_connection_sequence[i % expected_seq_length];
    if (actual != expected) {
      gpr_log(GPR_ERROR, "FAILURE: expected %d, actual %d at iter %d", expected,
              actual, i);
      print_failed_expectations(expected_connection_sequence,
                                actual_connection_sequence, expected_seq_length,
                                num_iters);
      abort();
    }
  }

  /* second half of the iterations go without response */
  for (; i < num_iters; i++) {
    GPR_ASSERT(actual_connection_sequence[i] == -1);
  }

  /* even though we know all the servers are dead, the client is still trying
   * retrying, believing it's in a transient failure situation */
  assert_channel_connectivity(client, 2, GRPC_CHANNEL_TRANSIENT_FAILURE,
                              GRPC_CHANNEL_CONNECTING);
  gpr_free(expected_connection_sequence);
}

static void verify_rebirth_round_robin(const servers_fixture *f,
                                       grpc_channel *client,
                                       const int *actual_connection_sequence,
                                       const size_t num_iters) {
  int *expected_connection_sequence;
  size_t i, j, unique_seq_last_idx, unique_seq_first_idx;
  const size_t expected_seq_length = f->num_servers;
  uint8_t *seen_elements;

  /* verify conn. seq. expectation */
  /* get the first unique run of length "num_servers". */
  expected_connection_sequence = gpr_malloc(sizeof(int) * expected_seq_length);
  seen_elements = gpr_malloc(sizeof(int) * expected_seq_length);

  unique_seq_last_idx = ~(size_t)0;

  memset(seen_elements, 0, sizeof(uint8_t) * expected_seq_length);
  for (i = 0; i < num_iters; i++) {
    if (actual_connection_sequence[i] < 0 ||
        seen_elements[actual_connection_sequence[i]] != 0) {
      /* if anything breaks the uniqueness of the run, back to square zero */
      memset(seen_elements, 0, sizeof(uint8_t) * expected_seq_length);
      continue;
    }
    seen_elements[actual_connection_sequence[i]] = 1;
    for (j = 0; j < expected_seq_length; j++) {
      if (seen_elements[j] == 0) break;
    }
    if (j == expected_seq_length) { /* seen all the elements */
      unique_seq_last_idx = i;
      break;
    }
  }
  /* make sure we found a valid run */
  for (j = 0; j < expected_seq_length; j++) {
    GPR_ASSERT(seen_elements[j] != 0);
  }

  GPR_ASSERT(unique_seq_last_idx != ~(size_t)0);

  unique_seq_first_idx = (unique_seq_last_idx - expected_seq_length + 1);
  memcpy(expected_connection_sequence,
         actual_connection_sequence + unique_seq_first_idx,
         sizeof(int) * expected_seq_length);

  /* first iteration succeeds */
  GPR_ASSERT(actual_connection_sequence[0] != -1);
  /* then we fail for a while... */
  GPR_ASSERT(actual_connection_sequence[1] == -1);
  /* ... but should be up at "unique_seq_first_idx" */
  GPR_ASSERT(actual_connection_sequence[unique_seq_first_idx] != -1);

  for (j = 0, i = unique_seq_first_idx; i < num_iters; i++) {
    const int actual = actual_connection_sequence[i];
    const int expected =
        expected_connection_sequence[j++ % expected_seq_length];
    if (actual != expected) {
      gpr_log(GPR_ERROR, "FAILURE: expected %d, actual %d at iter %d", expected,
              actual, i);
      print_failed_expectations(expected_connection_sequence,
                                actual_connection_sequence, expected_seq_length,
                                num_iters);
      abort();
    }
  }

  /* things are fine once the servers are brought back up */
  assert_channel_connectivity(client, 1, GRPC_CHANNEL_READY);
  gpr_free(expected_connection_sequence);
  gpr_free(seen_elements);
}

int main(int argc, char **argv) {
  test_spec *spec;
  size_t i;
  const size_t NUM_ITERS = 10;
  const size_t NUM_SERVERS = 4;

  grpc_test_init(argc, argv);
  grpc_init();

  /* everything is fine, all servers stay up the whole time and life's peachy */
  spec = test_spec_create(NUM_ITERS, NUM_SERVERS);
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

  /* at the start of the 2nd iteration, kill all but the first and last servers.
   * This should knock down the server bound to be selected next */
  test_spec_reset(spec);
  spec->verifier = verify_vanishing_floor_round_robin;
  spec->description = "test_kill_all_server_at_2nd_iteration";
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

  grpc_shutdown();
  return 0;
}
