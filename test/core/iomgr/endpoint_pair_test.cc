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

#include "src/core/lib/iomgr/endpoint_pair.h"

#include <chrono>

#include <gtest/gtest.h>

#include <grpc/event_engine/event_engine.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/shim.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "test/core/iomgr/endpoint_tests.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

using namespace std::chrono_literals;

namespace {

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::URIToResolvedAddress;

grpc_endpoint_pair grpc_iomgr_event_engine_shim_endpoint_pair(
    grpc_channel_args* c_args) {
  grpc_core::ExecCtx ctx;
  grpc_endpoint_pair p;
  auto ee = grpc_event_engine::experimental::GetDefaultEventEngine();
  auto memory_quota = std::make_unique<grpc_core::MemoryQuota>("bar");
  std::string target_addr = absl::StrCat(
      "ipv6:[::1]:", std::to_string(grpc_pick_unused_port_or_die()));
  auto resolved_addr = URIToResolvedAddress(target_addr);
  GPR_ASSERT(resolved_addr.ok());
  std::unique_ptr<EventEngine::Endpoint> client_endpoint;
  std::unique_ptr<EventEngine::Endpoint> server_endpoint;
  grpc_core::Notification client_signal;
  grpc_core::Notification server_signal;

  EventEngine::Listener::AcceptCallback accept_cb =
      [&server_endpoint, &server_signal](
          std::unique_ptr<EventEngine::Endpoint> ep,
          grpc_core::MemoryAllocator /*memory_allocator*/) {
        server_endpoint = std::move(ep);
        server_signal.Notify();
      };

  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(c_args);
  ChannelArgsEndpointConfig config(args);
  auto listener = *ee->CreateListener(
      std::move(accept_cb), [](absl::Status /*status*/) {}, config,
      std::make_unique<grpc_core::MemoryQuota>("foo"));

  GPR_ASSERT(listener->Bind(*resolved_addr).ok());
  GPR_ASSERT(listener->Start().ok());

  ee->Connect(
      [&client_endpoint, &client_signal](
          absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint) {
        GPR_ASSERT(endpoint.ok());
        client_endpoint = std::move(*endpoint);
        client_signal.Notify();
      },
      *resolved_addr, config, memory_quota->CreateMemoryAllocator("conn-1"),
      24h);

  client_signal.WaitForNotification();
  server_signal.WaitForNotification();

  p.client = grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
      std::move(client_endpoint));
  p.server = grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
      std::move(server_endpoint));
  return p;
}

}  // namespace

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;

static void clean_up(void) {}

static grpc_endpoint_test_fixture create_fixture_endpoint_pair(
    size_t slice_size) {
  grpc_core::ExecCtx exec_ctx;
  grpc_endpoint_test_fixture f;
  grpc_arg a[1];
  a[0].key = const_cast<char*>(GRPC_ARG_TCP_READ_CHUNK_SIZE);
  a[0].type = GRPC_ARG_INTEGER;
  a[0].value.integer = static_cast<int>(slice_size);
  grpc_channel_args args = {GPR_ARRAY_SIZE(a), a};
  grpc_endpoint_pair p;
  if (grpc_event_engine::experimental::UseEventEngineClient()) {
    p = grpc_iomgr_event_engine_shim_endpoint_pair(&args);
  } else {
    p = grpc_iomgr_create_endpoint_pair("test", &args);
  }
  f.client_ep = p.client;
  f.server_ep = p.server;
  grpc_endpoint_add_to_pollset(f.client_ep, g_pollset);
  grpc_endpoint_add_to_pollset(f.server_ep, g_pollset);

  return f;
}

static grpc_endpoint_test_config configs[] = {
    {"tcp/tcp_socketpair", create_fixture_endpoint_pair, clean_up},
};

static void destroy_pollset(void* p, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

TEST(EndpointPairTest, MainTest) {
#ifdef GPR_WINDOWS
  if (grpc_event_engine::experimental::UseEventEngineClient()) {
    gpr_log(GPR_INFO, "Skipping pathological EventEngine test on Windows");
    return;
  }
#endif
  grpc_closure destroyed;
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);
    grpc_endpoint_tests(configs[0], g_pollset, g_mu);
    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
  }
  grpc_shutdown();
  gpr_free(g_pollset);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
