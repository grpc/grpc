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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

void test_register_method_fail(void) {
  grpc_server *server = grpc_server_create(NULL, NULL);
  void *method;
  void *method_old;
  method =
      grpc_server_register_method(server, NULL, NULL, GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method == NULL);
  method_old =
      grpc_server_register_method(server, "m", "h", GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method_old != NULL);
  method = grpc_server_register_method(
      server, "m", "h", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 0);
  GPR_ASSERT(method == NULL);
  method_old =
      grpc_server_register_method(server, "m2", "h2", GRPC_SRM_PAYLOAD_NONE,
                                  GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
  GPR_ASSERT(method_old != NULL);
  method =
      grpc_server_register_method(server, "m2", "h2", GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method == NULL);
  method = grpc_server_register_method(
      server, "m2", "h2", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER,
      GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
  GPR_ASSERT(method == NULL);
  grpc_server_destroy(server);
}

void test_request_call_on_no_server_cq(void) {
  grpc_completion_queue *cc = grpc_completion_queue_create(NULL);
  grpc_server *server = grpc_server_create(NULL, NULL);
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE ==
             grpc_server_request_call(server, NULL, NULL, NULL, cc, cc, NULL));
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE ==
             grpc_server_request_registered_call(server, NULL, NULL, NULL, NULL,
                                                 NULL, cc, cc, NULL));
  grpc_completion_queue_destroy(cc);
  grpc_server_destroy(server);
}

void test_bind_server_twice(void) {
  grpc_arg a;
  a.type = GRPC_ARG_INTEGER;
  a.key = GRPC_ARG_ALLOW_REUSEPORT;
  a.value.integer = 0;
  grpc_channel_args args = {1, &a};

  char *addr;
  grpc_server *server1 = grpc_server_create(&args, NULL);
  grpc_server *server2 = grpc_server_create(&args, NULL);
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  int port = grpc_pick_unused_port_or_die();
  gpr_asprintf(&addr, "[::]:%d", port);
  grpc_server_register_completion_queue(server1, cq, NULL);
  grpc_server_register_completion_queue(server2, cq, NULL);
  GPR_ASSERT(0 == grpc_server_add_secure_http2_port(server2, addr, NULL));
  GPR_ASSERT(port == grpc_server_add_insecure_http2_port(server1, addr));
  GPR_ASSERT(0 == grpc_server_add_insecure_http2_port(server2, addr));
  grpc_server_credentials *fake_creds =
      grpc_fake_transport_security_server_credentials_create();
  GPR_ASSERT(0 == grpc_server_add_secure_http2_port(server2, addr, fake_creds));
  grpc_server_credentials_release(fake_creds);
  grpc_server_shutdown_and_notify(server1, cq, NULL);
  grpc_server_shutdown_and_notify(server2, cq, NULL);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
  grpc_server_destroy(server1);
  grpc_server_destroy(server2);
  grpc_completion_queue_destroy(cq);
  gpr_free(addr);
}

void test_bind_server_to_addr(const char *host, bool secure) {
  int port = grpc_pick_unused_port_or_die();
  char *addr;
  gpr_join_host_port(&addr, host, port);
  gpr_log(GPR_INFO, "Test bind to %s", addr);

  grpc_server *server = grpc_server_create(NULL, NULL);
  if (secure) {
    grpc_server_credentials *fake_creds =
        grpc_fake_transport_security_server_credentials_create();
    GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, fake_creds));
    grpc_server_credentials_release(fake_creds);
  } else {
    GPR_ASSERT(grpc_server_add_insecure_http2_port(server, addr));
  }
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  grpc_server_register_completion_queue(server, cq, NULL);
  grpc_server_start(server);
  grpc_server_shutdown_and_notify(server, cq, NULL);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), NULL);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  gpr_free(addr);
}

static int external_dns_works(const char *host) {
  grpc_resolved_addresses *res = NULL;
  grpc_error *error = grpc_blocking_resolve_address(host, "80", &res);
  GRPC_ERROR_UNREF(error);
  if (res != NULL) {
    grpc_resolved_addresses_destroy(res);
    return 1;
  }
  return 0;
}

static void test_bind_server_to_addrs(const char **addrs, size_t n) {
  for (size_t i = 0; i < n; i++) {
    test_bind_server_to_addr(addrs[i], false);
    test_bind_server_to_addr(addrs[i], true);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_register_method_fail();
  test_request_call_on_no_server_cq();
  test_bind_server_twice();

  static const char *addrs[] = {
      "::1", "127.0.0.1", "::ffff:127.0.0.1", "localhost", "0.0.0.0", "::",
  };
  test_bind_server_to_addrs(addrs, GPR_ARRAY_SIZE(addrs));

  if (external_dns_works("loopback46.unittest.grpc.io")) {
    static const char *dns_addrs[] = {
        "loopback46.unittest.grpc.io", "loopback4.unittest.grpc.io",
    };
    test_bind_server_to_addrs(dns_addrs, GPR_ARRAY_SIZE(dns_addrs));
  }

  grpc_shutdown();
  return 0;
}
