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

#include <stdio.h>
#include <string.h>

#include <grpc/byte_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "test/core/end2end/cq_verifier.h"

static void *tag(intptr_t t) { return (void *)t; }

static grpc_end2end_test_fixture begin_test(grpc_end2end_test_config config,
                                            const char *test_name,
                                            grpc_channel_args *client_args,
                                            grpc_channel_args *server_args) {
  grpc_end2end_test_fixture f;
  gpr_log(GPR_INFO, "%s/%s", test_name, config.name);
  f = config.create_fixture(client_args, server_args);
  config.init_server(&f, server_args);
  config.init_client(&f, client_args);
  return f;
}

static gpr_timespec n_seconds_time(int n) {
  return GRPC_TIMEOUT_SECONDS_TO_DEADLINE(n);
}

static gpr_timespec five_seconds_time(void) { return n_seconds_time(5); }

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, five_seconds_time(), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void shutdown_server(grpc_end2end_test_fixture *f) {
  if (!f->server) return;
  grpc_server_shutdown_and_notify(f->server, f->cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(
                 f->cq, tag(1000), GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5), NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(f->server);
  f->server = NULL;
}

static void shutdown_client(grpc_end2end_test_fixture *f) {
  if (!f->client) return;
  grpc_channel_destroy(f->client);
  f->client = NULL;
}

static void end_test(grpc_end2end_test_fixture *f) {
  shutdown_server(f);
  shutdown_client(f);

  grpc_completion_queue_shutdown(f->cq);
  drain_cq(f->cq);
  grpc_completion_queue_destroy(f->cq);
}

/* Creates and returns a gpr_slice containing random alphanumeric characters. */
static gpr_slice generate_random_slice() {
  size_t i;
  static const char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  char output[1024 * 1024];
  for (i = 0; i < GPR_ARRAY_SIZE(output) - 1; ++i) {
    output[i] = chars[rand() % (int)(sizeof(chars) - 1)];
  }
  output[GPR_ARRAY_SIZE(output) - 1] = '\0';
  return gpr_slice_from_copied_string(output);
}

void buffer_pool_server(grpc_end2end_test_config config) {
  grpc_buffer_pool *buffer_pool = grpc_buffer_pool_create("test_server");
  grpc_buffer_pool_resize(buffer_pool, 5 * 1024 * 1024);

#define NUM_CALLS 100
#define CLIENT_BASE_TAG 1000
#define SERVER_START_BASE_TAG 2000
#define SERVER_RECV_BASE_TAG 3000
#define SERVER_END_BASE_TAG 4000

  grpc_arg arg;
  arg.key = GRPC_ARG_BUFFER_POOL;
  arg.type = GRPC_ARG_POINTER;
  arg.value.pointer.p = buffer_pool;
  arg.value.pointer.vtable = grpc_buffer_pool_arg_vtable();
  grpc_channel_args args = {1, &arg};

  grpc_end2end_test_fixture f =
      begin_test(config, "buffer_pool_server", NULL, &args);

  /* Create large request and response bodies. These are big enough to require
   * multiple round trips to deliver to the peer, and their exact contents of
   * will be verified on completion. */
  gpr_slice request_payload_slice = generate_random_slice();

  grpc_call *client_calls[NUM_CALLS];
  grpc_call *server_calls[NUM_CALLS];
  grpc_metadata_array initial_metadata_recv[NUM_CALLS];
  grpc_metadata_array trailing_metadata_recv[NUM_CALLS];
  grpc_metadata_array request_metadata_recv[NUM_CALLS];
  grpc_call_details call_details[NUM_CALLS];
  grpc_status_code status[NUM_CALLS];
  char *details[NUM_CALLS];
  size_t details_capacity[NUM_CALLS];
  grpc_byte_buffer *request_payload_recv[NUM_CALLS];
  int was_cancelled[NUM_CALLS];
  grpc_call_error error;
  int pending_client_calls = 0;
  int pending_server_start_calls = 0;
  int pending_server_recv_calls = 0;
  int pending_server_end_calls = 0;
  int cancelled_calls_on_client = 0;
  int cancelled_calls_on_server = 0;

  grpc_byte_buffer *request_payload =
      grpc_raw_byte_buffer_create(&request_payload_slice, 1);

  grpc_op ops[6];
  grpc_op *op;

  for (int i = 0; i < NUM_CALLS; i++) {
    grpc_metadata_array_init(&initial_metadata_recv[i]);
    grpc_metadata_array_init(&trailing_metadata_recv[i]);
    grpc_metadata_array_init(&request_metadata_recv[i]);
    grpc_call_details_init(&call_details[i]);
    details[i] = NULL;
    details_capacity[i] = 0;
    request_payload_recv[i] = NULL;
    was_cancelled[i] = 0;
  }

  for (int i = 0; i < NUM_CALLS; i++) {
    error = grpc_server_request_call(
        f.server, &server_calls[i], &call_details[i], &request_metadata_recv[i],
        f.cq, f.cq, tag(SERVER_START_BASE_TAG + i));
    GPR_ASSERT(GRPC_CALL_OK == error);

    pending_server_start_calls++;
  }

  for (int i = 0; i < NUM_CALLS; i++) {
    client_calls[i] = grpc_channel_create_call(
        f.client, NULL, GRPC_PROPAGATE_DEFAULTS, f.cq, "/foo",
        "foo.test.google.fr", n_seconds_time(60), NULL);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message = request_payload;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata = &initial_metadata_recv[i];
    op->flags = 0;
    op->reserved = NULL;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata =
        &trailing_metadata_recv[i];
    op->data.recv_status_on_client.status = &status[i];
    op->data.recv_status_on_client.status_details = &details[i];
    op->data.recv_status_on_client.status_details_capacity =
        &details_capacity[i];
    op->flags = 0;
    op->reserved = NULL;
    op++;
    error = grpc_call_start_batch(client_calls[i], ops, (size_t)(op - ops),
                                  tag(CLIENT_BASE_TAG + i), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);

    pending_client_calls++;
  }

  while (pending_client_calls + pending_server_recv_calls +
             pending_server_end_calls >
         0) {
    grpc_event ev = grpc_completion_queue_next(f.cq, n_seconds_time(10), NULL);
    GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);

    int ev_tag = (int)(intptr_t)ev.tag;
    if (ev_tag < CLIENT_BASE_TAG) {
      abort(); /* illegal tag */
    } else if (ev_tag < SERVER_START_BASE_TAG) {
      /* client call finished */
      int call_id = ev_tag - CLIENT_BASE_TAG;
      GPR_ASSERT(call_id >= 0);
      GPR_ASSERT(call_id < NUM_CALLS);
      switch (status[call_id]) {
        case GRPC_STATUS_RESOURCE_EXHAUSTED:
          cancelled_calls_on_client++;
          break;
        case GRPC_STATUS_OK:
          break;
        default:
          gpr_log(GPR_ERROR, "Unexpected status code: %d", status[call_id]);
          abort();
      }
      GPR_ASSERT(pending_client_calls > 0);

      grpc_metadata_array_destroy(&initial_metadata_recv[call_id]);
      grpc_metadata_array_destroy(&trailing_metadata_recv[call_id]);
      grpc_call_destroy(client_calls[call_id]);
      gpr_free(details[call_id]);

      pending_client_calls--;
    } else if (ev_tag < SERVER_RECV_BASE_TAG) {
      /* new incoming call to the server */
      int call_id = ev_tag - SERVER_START_BASE_TAG;
      GPR_ASSERT(call_id >= 0);
      GPR_ASSERT(call_id < NUM_CALLS);

      memset(ops, 0, sizeof(ops));
      op = ops;
      op->op = GRPC_OP_SEND_INITIAL_METADATA;
      op->data.send_initial_metadata.count = 0;
      op->flags = 0;
      op->reserved = NULL;
      op++;
      op->op = GRPC_OP_RECV_MESSAGE;
      op->data.recv_message = &request_payload_recv[call_id];
      op->flags = 0;
      op->reserved = NULL;
      op++;
      error =
          grpc_call_start_batch(server_calls[call_id], ops, (size_t)(op - ops),
                                tag(SERVER_RECV_BASE_TAG + call_id), NULL);
      GPR_ASSERT(GRPC_CALL_OK == error);

      GPR_ASSERT(pending_server_start_calls > 0);
      pending_server_start_calls--;
      pending_server_recv_calls++;

      grpc_call_details_destroy(&call_details[call_id]);
      grpc_metadata_array_destroy(&request_metadata_recv[call_id]);
    } else if (ev_tag < SERVER_END_BASE_TAG) {
      /* finished read on the server */
      int call_id = ev_tag - SERVER_RECV_BASE_TAG;
      GPR_ASSERT(call_id >= 0);
      GPR_ASSERT(call_id < NUM_CALLS);

      if (ev.success) {
        if (request_payload_recv[call_id] != NULL) {
          grpc_byte_buffer_destroy(request_payload_recv[call_id]);
          request_payload_recv[call_id] = NULL;
        }
      } else {
        GPR_ASSERT(request_payload_recv[call_id] == NULL);
      }

      memset(ops, 0, sizeof(ops));
      op = ops;
      op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
      op->data.recv_close_on_server.cancelled = &was_cancelled[call_id];
      op->flags = 0;
      op->reserved = NULL;
      op++;
      op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
      op->data.send_status_from_server.trailing_metadata_count = 0;
      op->data.send_status_from_server.status = GRPC_STATUS_OK;
      op->data.send_status_from_server.status_details = "xyz";
      op->flags = 0;
      op->reserved = NULL;
      op++;
      error =
          grpc_call_start_batch(server_calls[call_id], ops, (size_t)(op - ops),
                                tag(SERVER_END_BASE_TAG + call_id), NULL);
      GPR_ASSERT(GRPC_CALL_OK == error);

      GPR_ASSERT(pending_server_recv_calls > 0);
      pending_server_recv_calls--;
      pending_server_end_calls++;
    } else {
      int call_id = ev_tag - SERVER_END_BASE_TAG;
      GPR_ASSERT(call_id >= 0);
      GPR_ASSERT(call_id < NUM_CALLS);

      if (was_cancelled[call_id]) {
        cancelled_calls_on_server++;
      }
      GPR_ASSERT(pending_server_end_calls > 0);
      pending_server_end_calls--;

      grpc_call_destroy(server_calls[call_id]);
    }
  }

  gpr_log(
      GPR_INFO,
      "Done. %d total calls: %d cancelled at server, %d cancelled at client.",
      NUM_CALLS, cancelled_calls_on_server, cancelled_calls_on_client);

  GPR_ASSERT(cancelled_calls_on_client >= cancelled_calls_on_server);
  GPR_ASSERT(cancelled_calls_on_server >= 0.9 * cancelled_calls_on_client);

  grpc_byte_buffer_destroy(request_payload);
  gpr_slice_unref(request_payload_slice);
  grpc_buffer_pool_unref(buffer_pool);

  end_test(&f);
  config.tear_down_data(&f);
}

void buffer_pool_server_pre_init(void) {}
