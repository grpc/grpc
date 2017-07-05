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

#include "src/core/lib/iomgr/port.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/support/string.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

/* This test exercises IPv4, IPv6, and dualstack sockets in various ways. */

static void *tag(intptr_t i) { return (void *)i; }

static gpr_timespec ms_from_now(int ms) {
  return grpc_timeout_milliseconds_to_deadline(ms);
}

static void drain_cq(grpc_completion_queue *cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, ms_from_now(5000), NULL);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void do_nothing(void *ignored) {}

void test_connect(const char *server_host, const char *client_host, int port,
                  int expect_ok) {
  char *client_hostport;
  char *server_hostport;
  grpc_channel *client;
  grpc_server *server;
  grpc_completion_queue *cq;
  grpc_completion_queue *shutdown_cq;
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
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  grpc_call_details call_details;
  char *peer;
  int picked_port = 0;

  if (port == 0) {
    port = grpc_pick_unused_port_or_die();
    picked_port = 1;
  }

  gpr_join_host_port(&server_hostport, server_host, port);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  /* Create server. */
  cq = grpc_completion_queue_create_for_next(NULL);
  server = grpc_server_create(NULL, NULL);
  grpc_server_register_completion_queue(server, cq, NULL);
  GPR_ASSERT((got_port = grpc_server_add_insecure_http2_port(
                  server, server_hostport)) > 0);
  if (port == 0) {
    port = got_port;
  } else {
    GPR_ASSERT(port == got_port);
  }
  grpc_server_start(server);
  cqv = cq_verifier_create(cq);

  /* Create client. */
  if (client_host[0] == 'i') {
    /* for ipv4:/ipv6: addresses, concatenate the port to each of the parts */
    size_t i;
    grpc_slice uri_slice;
    grpc_slice_buffer uri_parts;
    char **hosts_with_port;

    uri_slice =
        grpc_slice_new((char *)client_host, strlen(client_host), do_nothing);
    grpc_slice_buffer_init(&uri_parts);
    grpc_slice_split(uri_slice, ",", &uri_parts);
    hosts_with_port = gpr_malloc(sizeof(char *) * uri_parts.count);
    for (i = 0; i < uri_parts.count; i++) {
      char *uri_part_str = grpc_slice_to_c_string(uri_parts.slices[i]);
      gpr_asprintf(&hosts_with_port[i], "%s:%d", uri_part_str, port);
      gpr_free(uri_part_str);
    }
    client_hostport = gpr_strjoin_sep((const char **)hosts_with_port,
                                      uri_parts.count, ",", NULL);
    for (i = 0; i < uri_parts.count; i++) {
      gpr_free(hosts_with_port[i]);
    }
    gpr_free(hosts_with_port);
    grpc_slice_buffer_destroy(&uri_parts);
    grpc_slice_unref(uri_slice);
  } else {
    gpr_join_host_port(&client_hostport, client_host, port);
  }
  client = grpc_insecure_channel_create(client_hostport, NULL, NULL);

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
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr");
  c = grpc_channel_create_call(client, NULL, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               deadline, NULL);
  GPR_ASSERT(c);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = expect_ok ? GRPC_INITIAL_METADATA_WAIT_FOR_READY : 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = NULL;
  op++;
  error = grpc_call_start_batch(c, ops, (size_t)(op - ops), tag(1), NULL);
  GPR_ASSERT(GRPC_CALL_OK == error);

  if (expect_ok) {
    /* Check for a successful request. */
    error = grpc_server_request_call(server, &s, &call_details,
                                     &request_metadata_recv, cq, cq, tag(101));
    GPR_ASSERT(GRPC_CALL_OK == error);
    CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
    cq_verify(cqv);

    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op->flags = 0;
    op++;
    op->op = GRPC_OP_SEND_STATUS_FROM_SERVER;
    op->data.send_status_from_server.trailing_metadata_count = 0;
    op->data.send_status_from_server.status = GRPC_STATUS_UNIMPLEMENTED;
    grpc_slice status_details = grpc_slice_from_static_string("xyz");
    op->data.send_status_from_server.status_details = &status_details;
    op->flags = 0;
    op++;
    op->op = GRPC_OP_RECV_CLOSE_ON_SERVER;
    op->data.recv_close_on_server.cancelled = &was_cancelled;
    op->flags = 0;
    op++;
    error = grpc_call_start_batch(s, ops, (size_t)(op - ops), tag(102), NULL);
    GPR_ASSERT(GRPC_CALL_OK == error);

    CQ_EXPECT_COMPLETION(cqv, tag(102), 1);
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
    cq_verify(cqv);

    peer = grpc_call_get_peer(c);
    gpr_log(GPR_DEBUG, "got peer: '%s'", peer);
    gpr_free(peer);

    GPR_ASSERT(status == GRPC_STATUS_UNIMPLEMENTED);
    GPR_ASSERT(0 == grpc_slice_str_cmp(details, "xyz"));
    GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo"));
    GPR_ASSERT(0 ==
               grpc_slice_str_cmp(call_details.host, "foo.test.google.fr"));
    GPR_ASSERT(was_cancelled == 1);

    grpc_call_unref(s);
  } else {
    /* Check for a failed connection. */
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
    cq_verify(cqv);

    GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  }

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  /* Destroy client. */
  grpc_channel_destroy(client);

  /* Destroy server. */
  shutdown_cq = grpc_completion_queue_create_for_pluck(NULL);
  grpc_server_shutdown_and_notify(server, shutdown_cq, tag(1000));
  GPR_ASSERT(grpc_completion_queue_pluck(shutdown_cq, tag(1000),
                                         grpc_timeout_seconds_to_deadline(5),
                                         NULL)
                 .type == GRPC_OP_COMPLETE);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(shutdown_cq);
  grpc_completion_queue_shutdown(cq);
  drain_cq(cq);
  grpc_completion_queue_destroy(cq);

  grpc_metadata_array_destroy(&initial_metadata_recv);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_metadata_array_destroy(&request_metadata_recv);

  grpc_call_details_destroy(&call_details);
  grpc_slice_unref(details);
  if (picked_port) {
    grpc_recycle_unused_port(port);
  }
}

int external_dns_works(const char *host) {
  grpc_resolved_addresses *res = NULL;
  grpc_error *error = grpc_blocking_resolve_address(host, "80", &res);
  GRPC_ERROR_UNREF(error);
  if (res != NULL) {
    grpc_resolved_addresses_destroy(res);
    return 1;
  }
  return 0;
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
    test_connect("0.0.0.0", "ipv4:127.0.0.1,127.0.0.2,127.0.0.3", 0, 1);
    test_connect("0.0.0.0", "ipv6:[::ffff:127.0.0.1],[::ffff:127.0.0.2]", 0, 1);
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

    if (!external_dns_works("loopback46.unittest.grpc.io")) {
      gpr_log(GPR_INFO, "Skipping tests that depend on *.unittest.grpc.io.");
    } else {
      test_connect("loopback46.unittest.grpc.io", "loopback4.unittest.grpc.io",
                   0, 1);
      test_connect("loopback4.unittest.grpc.io", "loopback46.unittest.grpc.io",
                   0, 1);
      if (do_ipv6) {
        test_connect("loopback46.unittest.grpc.io",
                     "loopback6.unittest.grpc.io", 0, 1);
        test_connect("loopback6.unittest.grpc.io",
                     "loopback46.unittest.grpc.io", 0, 1);
        test_connect("loopback4.unittest.grpc.io", "loopback6.unittest.grpc.io",
                     0, 0);
        test_connect("loopback6.unittest.grpc.io", "loopback4.unittest.grpc.io",
                     0, 0);
      }
    }
  }

  grpc_shutdown();

  return 0;
}

#else /* GRPC_POSIX_SOCKET */

int main(int argc, char **argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET */
