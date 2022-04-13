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
#ifdef GRPC_POSIX_SOCKET_EV

#include <string.h>

#include <vector>

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/transport/error_utils.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

/* This test exercises IPv4, IPv6, and dualstack sockets in various ways. */

static void* tag(intptr_t t) { return reinterpret_cast<void*>(t); }

static void drain_cq(grpc_completion_queue* cq) {
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(
        cq, grpc_timeout_milliseconds_to_deadline(5000), nullptr);
  } while (ev.type != GRPC_QUEUE_SHUTDOWN);
}

static void log_resolved_addrs(const char* label, const char* hostname) {
  absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or =
      grpc_core::GetDNSResolver()->ResolveNameBlocking(hostname, "80");
  if (!addresses_or.ok()) {
    GRPC_LOG_IF_ERROR(hostname,
                      absl_status_to_grpc_error(addresses_or.status()));
    return;
  }
  for (const auto& addr : *addresses_or) {
    gpr_log(GPR_INFO, "%s: %s", label, grpc_sockaddr_to_uri(&addr).c_str());
  }
}

void test_connect(const char* server_host, const char* client_host, int port,
                  int expect_ok) {
  grpc_channel* client;
  grpc_server* server;
  grpc_completion_queue* cq;
  grpc_call* c;
  grpc_call* s;
  cq_verifier* cqv;
  gpr_timespec deadline;
  int got_port;
  grpc_op ops[6];
  grpc_op* op;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array request_metadata_recv;
  grpc_status_code status;
  grpc_call_error error;
  grpc_slice details;
  int was_cancelled = 2;
  grpc_call_details call_details;
  char* peer;
  int picked_port = 0;

  if (port == 0) {
    port = grpc_pick_unused_port_or_die();
    picked_port = 1;
  }

  std::string server_hostport = grpc_core::JoinHostPort(server_host, port);

  grpc_metadata_array_init(&initial_metadata_recv);
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_metadata_array_init(&request_metadata_recv);
  grpc_call_details_init(&call_details);

  /* Create server. */
  cq = grpc_completion_queue_create_for_next(nullptr);
  server = grpc_server_create(nullptr, nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  GPR_ASSERT((got_port = grpc_server_add_http2_port(
                  server, server_hostport.c_str(), server_creds)) > 0);
  grpc_server_credentials_release(server_creds);
  if (port == 0) {
    port = got_port;
  } else {
    GPR_ASSERT(port == got_port);
  }
  grpc_server_start(server);
  cqv = cq_verifier_create(cq);

  /* Create client. */
  std::string client_hostport;
  if (client_host[0] == 'i') {
    /* for ipv4:/ipv6: addresses, concatenate the port to each of the parts */
    std::vector<absl::string_view> uri_parts =
        absl::StrSplit(client_host, ',', absl::SkipEmpty());
    std::vector<std::string> hosts_with_port;
    hosts_with_port.reserve(uri_parts.size());
    for (const absl::string_view& uri_part : uri_parts) {
      hosts_with_port.push_back(absl::StrFormat("%s:%d", uri_part, port));
    }
    client_hostport = absl::StrJoin(hosts_with_port, ",");
  } else {
    client_hostport = grpc_core::JoinHostPort(client_host, port);
  }
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  client = grpc_channel_create(client_hostport.c_str(), creds, nullptr);
  grpc_channel_credentials_release(creds);

  gpr_log(GPR_INFO, "Testing with server=%s client=%s (expecting %s)",
          server_hostport.c_str(), client_hostport.c_str(),
          expect_ok ? "success" : "failure");
  log_resolved_addrs("server resolved addr", server_host);
  log_resolved_addrs("client resolved addr", client_host);

  if (expect_ok) {
    /* Normal deadline, shouldn't be reached. */
    deadline = grpc_timeout_milliseconds_to_deadline(60000);
  } else {
    /* Give up faster when failure is expected.
       BUG: Setting this to 1000 reveals a memory leak (b/18608927). */
    deadline = grpc_timeout_milliseconds_to_deadline(8000);
  }

  /* Send a trivial request. */
  grpc_slice host = grpc_slice_from_static_string("foo.test.google.fr");
  c = grpc_channel_create_call(client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
                               grpc_slice_from_static_string("/foo"), &host,
                               deadline, nullptr);
  GPR_ASSERT(c);

  memset(ops, 0, sizeof(ops));
  op = ops;
  op->op = GRPC_OP_SEND_INITIAL_METADATA;
  op->data.send_initial_metadata.count = 0;
  op->flags = expect_ok ? GRPC_INITIAL_METADATA_WAIT_FOR_READY : 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_INITIAL_METADATA;
  op->data.recv_initial_metadata.recv_initial_metadata = &initial_metadata_recv;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  op->data.recv_status_on_client.status = &status;
  op->data.recv_status_on_client.status_details = &details;
  op->flags = 0;
  op->reserved = nullptr;
  op++;
  error = grpc_call_start_batch(c, ops, static_cast<size_t>(op - ops), tag(1),
                                nullptr);
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
    error = grpc_call_start_batch(s, ops, static_cast<size_t>(op - ops),
                                  tag(102), nullptr);
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
    GPR_ASSERT(was_cancelled == 0);

    grpc_call_unref(s);
  } else {
    /* Check for a failed connection. */
    CQ_EXPECT_COMPLETION(cqv, tag(1), 1);
    cq_verify(cqv);

    gpr_log(GPR_INFO, "status: %d (expected: %d)", status,
            GRPC_STATUS_UNAVAILABLE);
    GPR_ASSERT(status == GRPC_STATUS_UNAVAILABLE);
  }

  grpc_call_unref(c);

  cq_verifier_destroy(cqv);

  /* Destroy client. */
  grpc_channel_destroy(client);

  /* Destroy server. */
  grpc_server_shutdown_and_notify(server, cq, tag(1000));
  grpc_event ev;
  do {
    ev = grpc_completion_queue_next(cq, grpc_timeout_seconds_to_deadline(5),
                                    nullptr);
  } while (ev.type != GRPC_OP_COMPLETE || ev.tag != tag(1000));

  grpc_server_destroy(server);
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

int external_dns_works(const char* host) {
  auto addresses_or =
      grpc_core::GetDNSResolver()->ResolveNameBlocking(host, "80");
  if (!addresses_or.ok()) {
    return 0;
  }
  int result = 1;
  for (const auto& addr : *addresses_or) {
    // Kokoro on Macservice uses Google DNS64 servers by default
    // (https://en.wikipedia.org/wiki/Google_Public_DNS) and that breaks
    // "dualstack_socket_test" due to loopback4.unittest.grpc.io resolving to
    // [64:ff9b::7f00:1]. (Working as expected for DNS64, but it prevents the
    // dualstack_socket_test from functioning correctly). See b/201064791.
    if (grpc_sockaddr_to_uri(&addr) == "ipv6:%5B64:ff9b::7f00:1%5D:80") {
      gpr_log(
          GPR_INFO,
          "Detected DNS64 server response. Tests that depend on "
          "*.unittest.grpc.io. will be skipped as they won't work with DNS64.");
      result = 0;
      break;
    }
  }
  return result;
}

int main(int argc, char** argv) {
  int do_ipv6 = 1;

  grpc::testing::TestEnvironment env(argc, argv);
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

    if (!external_dns_works("loopback4.unittest.grpc.io") ||
        !external_dns_works("loopback46.unittest.grpc.io")) {
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

#else /* GRPC_POSIX_SOCKET_EV */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_POSIX_SOCKET_EV */
