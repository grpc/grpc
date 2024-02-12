//
//
// Copyright 2016 gRPC authors.
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

#include <algorithm>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

typedef struct test_fixture {
  const char* name;
  void (*add_server_port)(grpc_server* server, const char* addr);
  // Have the creds here so all the channels will share the same one to enabled
  // subchannel sharing if needed.
  grpc_channel_credentials* creds;
} test_fixture;

#define NUM_CONNECTIONS 100

typedef struct {
  grpc_server* server;
  grpc_completion_queue* cq;
} server_thread_args;

static void server_thread_func(void* args) {
  server_thread_args* a = static_cast<server_thread_args*>(args);
  grpc_event ev = grpc_completion_queue_next(
      a->cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
  ASSERT_EQ(ev.tag, nullptr);
  ASSERT_EQ(ev.success, true);
}

static grpc_channel* create_test_channel(const char* addr,
                                         grpc_channel_credentials* creds,
                                         bool share_subchannel) {
  grpc_channel* channel = nullptr;
  std::vector<grpc_arg> args;
  args.push_back(grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL),
      !share_subchannel));
  if (creds != nullptr) {
    args.push_back(grpc_channel_arg_string_create(
        const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG),
        const_cast<char*>("foo.test.google.fr")));
  }
  grpc_channel_args channel_args = {args.size(), args.data()};
  if (creds != nullptr) {
    channel = grpc_channel_create(addr, creds, &channel_args);
  } else {
    grpc_channel_credentials* insecure_creds =
        grpc_insecure_credentials_create();
    channel = grpc_channel_create(addr, insecure_creds, &channel_args);
    grpc_channel_credentials_release(insecure_creds);
  }
  return channel;
}

static void run_test(const test_fixture* fixture, bool share_subchannel) {
  gpr_log(GPR_INFO, "TEST: %s sharing subchannel: %d", fixture->name,
          share_subchannel);

  std::string addr =
      grpc_core::JoinHostPort("localhost", grpc_pick_unused_port_or_die());

  grpc_server* server = grpc_server_create(nullptr, nullptr);
  fixture->add_server_port(server, addr.c_str());
  grpc_completion_queue* server_cq =
      grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(server, server_cq, nullptr);
  grpc_server_start(server);

  server_thread_args sta = {server, server_cq};
  grpc_core::Thread server_thread("grpc_server", server_thread_func, &sta);
  server_thread.Start();

  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_channel* channels[NUM_CONNECTIONS];
  for (size_t i = 0; i < NUM_CONNECTIONS; i++) {
    channels[i] =
        create_test_channel(addr.c_str(), fixture->creds, share_subchannel);

    gpr_timespec connect_deadline = grpc_timeout_seconds_to_deadline(30);
    grpc_connectivity_state state;
    while ((state = grpc_channel_check_connectivity_state(channels[i], 1)) !=
           GRPC_CHANNEL_READY) {
      grpc_channel_watch_connectivity_state(channels[i], state,
                                            connect_deadline, cq, nullptr);
      grpc_event ev = grpc_completion_queue_next(
          cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
      // check that the watcher from "watch state" was free'd
      ASSERT_EQ(grpc_channel_num_external_connectivity_watchers(channels[i]),
                0);
      ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
      ASSERT_EQ(ev.tag, nullptr);
      ASSERT_EQ(ev.success, true);
    }
  }

  grpc_server_shutdown_and_notify(server, server_cq, nullptr);
  server_thread.Join();

  grpc_completion_queue_shutdown(server_cq);
  grpc_completion_queue_shutdown(cq);

  while (grpc_completion_queue_next(server_cq,
                                    gpr_inf_future(GPR_CLOCK_REALTIME), nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }
  while (grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr)
             .type != GRPC_QUEUE_SHUTDOWN) {
  }

  for (size_t i = 0; i < NUM_CONNECTIONS; i++) {
    grpc_channel_destroy(channels[i]);
  }

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(server_cq);
  grpc_completion_queue_destroy(cq);
}

static void insecure_test_add_port(grpc_server* server, const char* addr) {
  grpc_server_credentials* server_creds =
      grpc_insecure_server_credentials_create();
  grpc_server_add_http2_port(server, addr, server_creds);
  grpc_server_credentials_release(server_creds);
}

static void secure_test_add_port(grpc_server* server, const char* addr) {
  std::string server_cert =
      grpc_core::testing::GetFileContents(SERVER_CERT_PATH);
  std::string server_key = grpc_core::testing::GetFileContents(SERVER_KEY_PATH);
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key.c_str(),
                                                  server_cert.c_str()};
  grpc_server_credentials* ssl_creds = grpc_ssl_server_credentials_create(
      nullptr, &pem_key_cert_pair, 1, 0, nullptr);
  grpc_server_add_http2_port(server, addr, ssl_creds);
  grpc_server_credentials_release(ssl_creds);
}

TEST(SequentialConnectivityTest, MainTest) {
  grpc_init();

  const test_fixture insecure_test = {
      "insecure",
      insecure_test_add_port,
      nullptr,
  };
  run_test(&insecure_test, /*share_subchannel=*/true);
  run_test(&insecure_test, /*share_subchannel=*/false);

  std::string test_root_cert =
      grpc_core::testing::GetFileContents(CA_CERT_PATH);
  grpc_channel_credentials* ssl_creds = grpc_ssl_credentials_create(
      test_root_cert.c_str(), nullptr, nullptr, nullptr);
  const test_fixture secure_test = {
      "secure",
      secure_test_add_port,
      ssl_creds,
  };
  run_test(&secure_test, /*share_subchannel=*/true);
  run_test(&secure_test, /*share_subchannel=*/false);
  grpc_channel_credentials_release(ssl_creds);

  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
