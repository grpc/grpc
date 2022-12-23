//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <stddef.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/host_port.h"
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
  ASSERT_EQ(method, nullptr);
  method_old =
      grpc_server_register_method(server, "m", "h", GRPC_SRM_PAYLOAD_NONE, 0);
  ASSERT_NE(method_old, nullptr);
  method = grpc_server_register_method(
      server, "m", "h", GRPC_SRM_PAYLOAD_READ_INITIAL_BYTE_BUFFER, 0);
  ASSERT_EQ(method, nullptr);
  grpc_server_destroy(server);
}

void test_request_call_on_no_server_cq(void) {
  grpc_completion_queue* cc = grpc_completion_queue_create_for_next(nullptr);
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  ASSERT_EQ(GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE,
            grpc_server_request_call(server, nullptr, nullptr, nullptr, cc, cc,
                                     nullptr));
  ASSERT_EQ(
      GRPC_CALL_ERROR_NOT_SERVER_COMPLETION_QUEUE,
      grpc_server_request_registered_call(server, nullptr, nullptr, nullptr,
                                          nullptr, nullptr, cc, cc, nullptr));
  grpc_completion_queue_destroy(cc);
  grpc_server_destroy(server);
}

void test_bind_server_twice(void) {
  grpc_arg a = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ALLOW_REUSEPORT), 0);
  grpc_channel_args args = {1, &a};

  grpc_server* server1 = grpc_server_create(&args, nullptr);
  grpc_server* server2 = grpc_server_create(&args, nullptr);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  int port = grpc_pick_unused_port_or_die();
  std::string addr = absl::StrCat("[::]:", port);
  grpc_server_register_completion_queue(server1, cq, nullptr);
  grpc_server_register_completion_queue(server2, cq, nullptr);
  ASSERT_EQ(0, grpc_server_add_http2_port(server2, addr.c_str(), nullptr));
  grpc_server_credentials* insecure_creds =
      grpc_insecure_server_credentials_create();
  ASSERT_EQ(port,
            grpc_server_add_http2_port(server1, addr.c_str(), insecure_creds));
  grpc_server_credentials_release(insecure_creds);
  grpc_server_credentials* another_insecure_creds =
      grpc_insecure_server_credentials_create();
  ASSERT_EQ(0, grpc_server_add_http2_port(server2, addr.c_str(),
                                          another_insecure_creds));
  grpc_server_credentials_release(another_insecure_creds);
  grpc_server_credentials* fake_creds =
      grpc_fake_transport_security_server_credentials_create();
  ASSERT_EQ(0, grpc_server_add_http2_port(server2, addr.c_str(), fake_creds));
  grpc_server_credentials_release(fake_creds);
  grpc_server_shutdown_and_notify(server1, cq, nullptr);
  grpc_server_shutdown_and_notify(server2, cq, nullptr);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  grpc_server_destroy(server1);
  grpc_server_destroy(server2);
  grpc_completion_queue_destroy(cq);
}

void test_bind_server_to_addr(const char* host, bool secure) {
  int port = grpc_pick_unused_port_or_die();
  std::string addr = grpc_core::JoinHostPort(host, port);
  gpr_log(GPR_INFO, "Test bind to %s", addr.c_str());

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  if (secure) {
    grpc_server_credentials* fake_creds =
        grpc_fake_transport_security_server_credentials_create();
    ASSERT_TRUE(grpc_server_add_http2_port(server, addr.c_str(), fake_creds));
    grpc_server_credentials_release(fake_creds);
  } else {
    grpc_server_credentials* insecure_creds =
        grpc_insecure_server_credentials_create();
    ASSERT_TRUE(
        grpc_server_add_http2_port(server, addr.c_str(), insecure_creds));
    grpc_server_credentials_release(insecure_creds);
  }
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);
  grpc_server_start(server);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
}

static bool external_dns_works(const char* host) {
  return grpc_core::GetDNSResolver()->LookupHostnameBlocking(host, "80").ok();
}

static void test_bind_server_to_addrs(const char** addrs, size_t n) {
  for (size_t i = 0; i < n; i++) {
    test_bind_server_to_addr(addrs[i], false);
    test_bind_server_to_addr(addrs[i], true);
  }
}

TEST(ServerTest, MainTest) {
  grpc_init();
  test_register_method_fail();
  test_request_call_on_no_server_cq();
  test_bind_server_twice();

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
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
