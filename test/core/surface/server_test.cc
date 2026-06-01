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

#include "src/core/server/server.h"

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/time.h>
#include <stddef.h>

#include <memory>
#include <string>

#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/event_engine/utils.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/host_port.h"
#include "src/core/util/useful.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

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
  LOG(INFO) << "Test bind to " << addr;

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
  if (grpc_core::IsEventEngineDnsNonClientChannelEnabled() ||
      grpc_event_engine::experimental::
          EventEngineExperimentDisabledForPython()) {
    auto resolver =
        grpc_event_engine::experimental::GetDefaultEventEngine()
            ->GetDNSResolver(grpc_event_engine::experimental::EventEngine::
                                 DNSResolver::ResolverOptions());
    if (!resolver.ok()) return false;
    return grpc_event_engine::experimental::LookupHostnameBlocking(
               resolver->get(), host, "80")
        .ok();
  } else {
    return grpc_core::GetDNSResolver()->LookupHostnameBlocking(host, "80").ok();
  }
}

static void test_bind_server_to_addrs(const char** addrs, size_t n) {
  for (size_t i = 0; i < n; i++) {
    test_bind_server_to_addr(addrs[i], false);
    test_bind_server_to_addr(addrs[i], true);
  }
}

TEST(ServerTest, StatsPluginGroupPlumbed) {
  grpc_server* server = grpc_server_create(nullptr, nullptr);
  grpc_core::Server* core_server = grpc_core::Server::FromC(server);
  auto stats_plugin_group =
      core_server->channel_args()
          .GetObject<grpc_core::GlobalStatsPluginRegistry::StatsPluginGroup>();
  ASSERT_NE(stats_plugin_group, nullptr);
  grpc_server_destroy(server);
}

TEST(ServerTest, RejectBacklogMatchOrQueue) {
  grpc_init();
  // Configure server limits to trigger deterministic backlog overflow.
  // soft_limit = 0, hard_limit = 1
  // - For queue size = 0, Reject size <= soft_limit (0 <= 0) returns false
  // (allow).
  // - For queue size = 1, Reject size >= hard_limit (1 >= 1) returns true
  // (reject).
  grpc_arg args_list[2];
  args_list[0] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_SERVER_MAX_PENDING_REQUESTS), 0);
  args_list[1] = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_SERVER_MAX_PENDING_REQUESTS_HARD_LIMIT), 1);
  grpc_channel_args channel_args = {2, args_list};

  grpc_server* server = grpc_server_create(&channel_args, nullptr);
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(server, cq, nullptr);

  int port = grpc_pick_unused_port_or_die();
  std::string addr = grpc_core::JoinHostPort("localhost", port);
  grpc_server_credentials* insecure_creds =
      grpc_insecure_server_credentials_create();
  ASSERT_TRUE(grpc_server_add_http2_port(server, addr.c_str(), insecure_creds));
  grpc_server_credentials_release(insecure_creds);
  grpc_server_start(server);

  grpc_channel_credentials* client_creds = grpc_insecure_credentials_create();
  grpc_channel* client =
      grpc_channel_create(addr.c_str(), client_creds, nullptr);
  grpc_channel_credentials_release(client_creds);
  ASSERT_TRUE(client);

  grpc_slice host = grpc_slice_from_static_string("localhost");
  // First Incoming Call: queue size is 0. Since 0 <= soft_limit (0 <= 0) is
  // true, the call is accepted and placed in the backlog, raising backlog size
  // to 1.
  grpc_call* call1 = grpc_channel_create_call(
      client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/TestMethod"), &host,
      grpc_timeout_seconds_to_deadline(5), nullptr);
  ASSERT_TRUE(call1);

  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[0].data.send_initial_metadata.count = 0;
  ops[0].flags = 0;
  ops[0].reserved = nullptr;
  grpc_call_error error = grpc_call_start_batch(
      call1, ops, 1, reinterpret_cast<void*>(101), nullptr);
  ASSERT_EQ(GRPC_CALL_OK, error);

  grpc_event ev = grpc_completion_queue_next(
      cq, gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
  ASSERT_EQ(ev.tag, reinterpret_cast<void*>(101));
  ASSERT_TRUE(ev.success);

  // Second Incoming Call: backlog size is now 1. Since 1 >= hard_limit (1 >= 1)
  // evaluates to true, the backlog protector's Reject() deterministically
  // returns true, and the call is immediately rejected via FailCallCreation().
  grpc_call* call2 = grpc_channel_create_call(
      client, nullptr, GRPC_PROPAGATE_DEFAULTS, cq,
      grpc_slice_from_static_string("/TestMethod"), &host,
      grpc_timeout_seconds_to_deadline(5), nullptr);
  ASSERT_TRUE(call2);

  grpc_metadata_array trailing_metadata_recv;
  grpc_metadata_array_init(&trailing_metadata_recv);
  grpc_status_code status = GRPC_STATUS_OK;
  grpc_slice details = grpc_empty_slice();

  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[0].data.send_initial_metadata.count = 0;
  ops[0].flags = 0;
  ops[0].reserved = nullptr;
  ops[1].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[1].data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
  ops[1].data.recv_status_on_client.status = &status;
  ops[1].data.recv_status_on_client.status_details = &details;

  error = grpc_call_start_batch(call2, ops, 2, reinterpret_cast<void*>(102),
                                nullptr);
  ASSERT_EQ(GRPC_CALL_OK, error);

  ev = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC),
                                  nullptr);
  ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
  ASSERT_EQ(ev.tag, reinterpret_cast<void*>(102));
  ASSERT_TRUE(ev.success);

  // Verify that the second call completed failed (status is not OK).
  EXPECT_NE(status, GRPC_STATUS_OK);

  grpc_call_unref(call2);
  grpc_metadata_array_destroy(&trailing_metadata_recv);
  grpc_slice_unref(details);

  grpc_call_cancel(call1, nullptr);
  grpc_call_unref(call1);

  grpc_server_shutdown_and_notify(server, cq, reinterpret_cast<void*>(1000));
  ev = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_MONOTONIC),
                                  nullptr);
  ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
  ASSERT_EQ(ev.tag, reinterpret_cast<void*>(1000));

  grpc_server_destroy(server);
  grpc_channel_destroy(client);
  grpc_completion_queue_destroy(cq);
  grpc_shutdown();
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
  grpc_core::ForceEnableExperiment("optimization_04", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
