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

#include <string.h>
#include "src/core/iomgr/socket_utils_posix.h"
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

/* This test exercises IPv4, IPv6, and dualstack sockets in various ways. */

static void *tag(gpr_intptr i) { return (void *)i; }

static gpr_timespec ms_from_now(int ms) {
  return GRPC_TIMEOUT_MILLIS_TO_DEADLINE(ms);
}

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, ms_from_now(5000));
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

void test_connect(const char *server_host, const char *client_host, int port,
                  int expect_ok) {
  char *client_hostport;
  char *server_hostport;
  grpc_channel *client;
  grpc_server *server;
  grpc_completion_queue *cq;
  grpc_call *c;
  grpc_call *s;
  cq_verifier *cqv;
  gpr_timespec deadline;
  int got_port;
  grpc_op ops[6];
  grpc_op *op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_status_code status;
  char *details = NULL;
  size_t details_capacity = 0;
  int was_cancelled = 2;
  grpc_call_details call_details;
  char *peer;

  if (port == 0) {
    port = grpc_pick_unused_port_or_die();
  }

  gpr_join_host_port(&server_hostport, server_host, port);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  /* Create server. */
  cq = grpc_completion_queue_create();
  server = grpc_server_create(NULL);
  grpc_server_register_completion_queue(server, cq);
  GPR_ASSERT((got_port = grpc_server_add_http2_port(server, server_hostport)) >
             0);
  if (port == 0) {
    port = got_port;
  } else {
    GPR_ASSERT(port == got_port);
  }
  grpc_server_start(server);
  cqv = cq_verifier_create(cq);

  /* Create client. */
  if (client_host[0] == 'i') {
    /* for ipv4:/ipv6: addresses, just concatenate the port */
    gpr_asprintf(&client_hostport, "%s:%d", client_host, port);
  } else {
    gpr_join_host_port(&client_hostport, client_host, port);
  }
  client = grpc_channel_create(client_hostport, NULL);

  gpr_log(GPR_INFO, "Testing with server=%s client=%s (expecting %s)",
          server_hostport, client_hostport, expect_ok ? "success" : "failure");

  gpr_free(client_hostport);
  gpr_free(server_hostport);

  if (expect_ok) {
    /* Normal deadline, shouldn't be reached. */
    deadline = ms_from_now(60000);
  } else {
    /* Give up faster when failure is expected.
       BUG: Setting this to 1000 reveals a memory leak (b/18608927). */
    deadline = ms_from_now(1500);
  }

  /* Send a trivial request. */
  c = grpc_channel_create_call(client, cq, "/foo", "foo.test.google.fr",
                               deadline);
  GPR_ASSERT(c);

  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = 0;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->data.recv_status_on_client.status_details_capacity = &details_capacity;
  op->flags = 0;
  op++;
  GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(c, ops, op - ops, tag(1)));

  if (expect_ok) {
    /* Check for a successful request. */
    GPR_ASSERT(GRPC_CALL_OK == grpc_server_request_call(
                                   server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101)));
    cq_expect_completion(cqv, tag(101), 1);
    cq_verify(cqv);

    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op++;
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.trailing_metadata_count = 0;
    op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
    op->data.send_status_from_server.status_details = "xyz";
    op->flags = 0;
    op++;
    op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op->data.recv_close_on_server.cancelled = &was_cancelled;
    op->flags = 0;
    op++;
    GPR_ASSERT(GRPC_CALL_OK ==
               grpc_call_start_batch(s, ops, op - ops, tag(102)));

    cq_expect_completion(cqv, tag(102), 1);
    cq_expect_completion(cqv, tag(1), 1);
    cq_verify(cqv);

    peer = grpc_call_get_peer(c);
    gpr_log(GPR_DEBUG, "got peer: '%s'", peer);
    gpr_free(peer);

    GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
    GPR_ASSERT(0 == strcmp(details, "xyz"));
    GPR_ASSERT(0 == strcmp(call_details.method, "/foo"));
    GPR_ASSERT(0 == strcmp(call_details.host, "foo.test.google.fr"));
    GPR_ASSERT(was_cancelled == 1);

    grpc_call_destroy(s);
  } else {
    /* Check for a failed connection. */
    cq_expect_completion(cqv, tag(1), 1);
    cq_verify(cqv);

    GPR_ASSERT(status == GRPC_STATUS_DEADLINE_EXCEEDED);
  }

  grpc_call_destroy(c);

  cq_verifier_destroy(cqv);

  /* Destroy client. */
  grpc_channel_destroy(client);

  /* Destroy server. */
  grpc_server_shutdown_and_notify(server, cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(cq, tag(1000),
                                         GRPC_TIMEOUT_SECONDS_TO_DEADLINE(5))
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(server);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);

  grpc_call_details_destroy(&call_details);
  gpr_free(details);
}

int main(int argc, char **argv) {
  int do_ipv6 = 1;

  grpc_test_init(argc, argv);
  grpc_init();

  if (!grpc_ipv6_loopback_available()) {
    gpr_log(GPR_INFO, "Can't bind to ::1.  Skipping IPv6 tests.");
    do_ipv6 = 0;
  }

  /* For coverage, test with and without dualstack sockets. */
  for (grpc_forbid_dualstack_sockets_for_testing = 0;
       grpc_forbid_dualstack_sockets_for_testing <= 1;
       grpc_forbid_dualstack_sockets_for_testing++) {
    /* :: and 0.0.0.0 are handled identically. */
    test_connect("::", "127.0.0.1", 0, 1);
    test_connect("::", "::ffff:127.0.0.1", 0, 1);
    test_connect("::", "ipv4:127.0.0.1", 0, 1);
    test_connect("::", "ipv6:[::ffff:127.0.0.1]", 0, 1);
    test_connect("::", "localhost", 0, 1);
    test_connect("0.0.0.0", "127.0.0.1", 0, 1);
    test_connect("0.0.0.0", "::ffff:127.0.0.1", 0, 1);
    test_connect("0.0.0.0", "ipv4:127.0.0.1", 0, 1);
    test_connect("0.0.0.0", "ipv6:[::ffff:127.0.0.1]", 0, 1);
    test_connect("0.0.0.0", "localhost", 0, 1);
    if (do_ipv6) {
      test_connect("::", "::1", 0, 1);
      test_connect("0.0.0.0", "::1", 0, 1);
      test_connect("::", "ipv6:[::1]", 0, 1);
      test_connect("0.0.0.0", "ipv6:[::1]", 0, 1);
    }

    /* These only work when the families agree. */
    test_connect("127.0.0.1", "127.0.0.1", 0, 1);
    test_connect("127.0.0.1", "ipv4:127.0.0.1", 0, 1);
    if (do_ipv6) {
      test_connect("::1", "::1", 0, 1);
      test_connect("::1", "127.0.0.1", 0, 0);
      test_connect("127.0.0.1", "::1", 0, 0);
      test_connect("::1", "ipv6:[::1]", 0, 1);
      test_connect("::1", "ipv4:127.0.0.1", 0, 0);
      test_connect("127.0.0.1", "ipv6:[::1]", 0, 0);
    }
  }

  grpc_shutdown();

  return 0;
}
