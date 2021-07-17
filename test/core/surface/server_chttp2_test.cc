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

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/tsi/fake_transport_security.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

TEST(ServerChttp2, UnparseableTarget) {
  grpc_channel_args args = {0, nullptr};
  grpc_server* server = grpc_server_create(&args, nullptr);
  int port = grpc_server_add_insecure_http2_port(server, "[");
  EXPECT_EQ(port, 0);
  grpc_server_destroy(server);
}

// GRPC_ARG_ALLOW_REUSEPORT isn't supported for custom servers
#ifndef GRPC_UV
TEST(ServerChttp2, AddSamePortTwice) {
  grpc_arg a = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_ALLOW_REUSEPORT), 0);
  grpc_channel_args args = {1, &a};

  int port = grpc_pick_unused_port_or_die();
  grpc_completion_queue* cq = grpc_completion_queue_create_for_pluck(nullptr);
  grpc_server* server = grpc_server_create(&args, nullptr);
  grpc_server_credentials* fake_creds =
      grpc_fake_transport_security_server_credentials_create();
  std::string addr = grpc_core::JoinHostPort("localhost", port);
  EXPECT_EQ(grpc_server_add_secure_http2_port(server, addr.c_str(), fake_creds),
            port);
  EXPECT_EQ(grpc_server_add_secure_http2_port(server, addr.c_str(), fake_creds),
            0);

  grpc_server_credentials_release(fake_creds);
  grpc_server_shutdown_and_notify(server, cq, nullptr);
  grpc_completion_queue_pluck(cq, nullptr, gpr_inf_future(GPR_CLOCK_REALTIME),
                              nullptr);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq);
}
#endif

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
