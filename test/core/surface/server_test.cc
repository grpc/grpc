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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

void test_register_method_fail(void) {
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  void* method;
  void* method_old;
  method = grpc_server_register_method(server, nullptr, nullptr,
                                       GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method == nullptr);
  method_old =
      grpc_server_register_method(server, "m", "h", GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method_old != nullptr);
  method = grpc_server_register_method(
      server, "m", "h", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 0);
  GPR_ASSERT(method == nullptr);
  method_old =
      grpc_server_register_method(server, "m2", "h2", GRPC_SRM_PAYLOAD_NONE,
                                  GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
  GPR_ASSERT(method_old != nullptr);
  method =
      grpc_server_register_method(server, "m2", "h2", GRPC_SRM_PAYLOAD_NONE, 0);
  GPR_ASSERT(method == nullptr);
  method = grpc_server_register_method(
      server, "m2", "h2", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER,
      GRPC_INITIAL_METADATA_IDEMPOTENT_REQUEST);
  GPR_ASSERT(method == nullptr);
  grpc_server_destroy(server);
}

void test_request_call_on_no_server_cq(void) {
  grpc_completion_queue* cc = grpc_completion_queue_create_for_next(nullptr);
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE ==
             grpc_server_request_call(server, nullptr, nullptr, nullptr, cc, cc,
                                      nullptr));
  GPR_ASSERT(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE ==
             grpc_server_request_registered_call(server, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr, cc,
                                                 cc, nullptr));
  grpc_completion_queue_destroy(cc);
  grpc_server_destroy(server);
}

// GRPC_ARG_ALLOW_REUSEPORT isn't supported for custom servers
#ifndef GRPC_UV
void test_bind_server_twice(void) {
  grpc_arg a;
  a.type = GRPC_ARG_INTEGER;
  a.key = const_cast<char*>(GRPC_ARG_ALLOW_REUSEPORT);
  a.value.integer = 0;
  grpc_channel_args args = {1, &a};

  char* addr;
  grpc_server* server1 = grpc_server_create(&args, nullptr);
  grpc_server* server2 = grpc_server_create(&args, nullptr);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  int port = grpc_pick_unused_port_or_die();
  gpr_asprintf(&addr, "[::]:%d", port);
  grpc_server_register_completion_queue(server1, cq, nullptr);
  grpc_server_register_completion_queue(server2, cq, nullptr);
  GPR_ASSERT(0 == grpc_server_add_secure_http2_port(server2, addr, nullptr));
  GPR_ASSERT(port == grpc_server_add_insecure_http2_port(server1, addr));
  GPR_ASSERT(0 == grpc_server_add_insecure_http2_port(server2, addr));
  grpc_server_credentials* fake_creds =
      grpc_fake_transport_security_server_credentials_create();
  GPR_ASSERT(0 == grpc_server_add_secure_http2_port(server2, addr, fake_creds));
  grpc_server_credentials_release(fake_creds);
  grpc_server_shutdown_and_notify(server1, cq, nullptr);
  grpc_server_shutdown_and_notify(server2, cq, nullptr);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  grpc_server_destroy(server1);
  grpc_server_destroy(server2);
  grpc_completion_queue_destroy(cq);
  gpr_free(addr);
}
#endif

void test_bind_server_to_addr(const char* host, bool secure) {
  int port = grpc_pick_unused_port_or_die();
  char* addr;
  gpr_join_host_port(&addr, host, port);
  gpr_log(GPR_INFO, "Test bind to %s", addr);

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  if (secure) {
    grpc_server_credentials* fake_creds =
        grpc_fake_transport_security_server_credentials_create();
    GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, fake_creds));
    grpc_server_credentials_release(fake_creds);
  } else {
    GPR_ASSERT(grpc_server_add_insecure_http2_port(server, addr));
  }
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_start(server);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
  gpr_free(addr);
}

static int external_dns_works(const char* host) {
  grpc_resolved_addresses* res = nullptr;
  grpc_error* error = grpc_blocking_resolve_address(host, "80", &res);
  GRPC_ERROR_UNREF(error);
  if (res != nullptr) {
    grpc_resolved_addresses_destroy(res);
    return 1;
  }
  return 0;
}

static void test_bind_server_to_addrs(const char** addrs, size_t n) {
  for (size_t i = 0; i < n; i++) {
    test_bind_server_to_addr(addrs[i], false);
    test_bind_server_to_addr(addrs[i], true);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_register_method_fail();
  test_request_call_on_no_server_cq();
#ifndef GRPC_UV
  test_bind_server_twice();
#endif

  static const char* addrs[] = {
      "::1", "127.0.0.1", "::ffff:127.0.0.1", "localhost", "0.0.0.0", "::",
  };
  test_bind_server_to_addrs(addrs, GPR_ARRAY_SIZE(addrs));

  if (external_dns_works("loopback46.unittest.grpc.io")) {
    static const char* dns_addrs[] = {
        "loopback46.unittest.grpc.io",
        "loopback4.unittest.grpc.io",
    };
    test_bind_server_to_addrs(dns_addrs, GPR_ARRAY_SIZE(dns_addrs));
  }

  grpc_shutdown();
  return 0;
}
