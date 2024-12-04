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

#include "test/core/test_util/test_tcp_server.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/util/time.h"
#include "test/core/test_util/test_config.h"

static void on_server_destroyed(void* data, grpc_error_handle /*error*/) {
  test_tcp_server* server = static_cast<test_tcp_server*>(data);
  gpr_mu_lock(server->mu);
  server->shutdown = true;
  gpr_mu_unlock(server->mu);
}

void test_tcp_server_init(test_tcp_server* server,
                          grpc_tcp_server_cb on_connect, void* user_data) {
  grpc_init();
  GRPC_CLOSURE_INIT(&server->shutdown_complete, on_server_destroyed, server,
                    grpc_schedule_on_exec_ctx);

  grpc_pollset* pollset =
      static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(pollset, &server->mu);
  server->pollset.push_back(pollset);
  server->on_connect = on_connect;
  server->cb_data = user_data;
}

void test_tcp_server_start(test_tcp_server* server, int port) {
  grpc_resolved_address resolved_addr;
  grpc_sockaddr_in* addr =
      reinterpret_cast<grpc_sockaddr_in*>(resolved_addr.addr);
  int port_added;
  grpc_core::ExecCtx exec_ctx;

  addr->sin_family = GRPC_AF_INET;
  addr->sin_port = grpc_htons(static_cast<uint16_t>(port));
  memset(&addr->sin_addr, 0, sizeof(addr->sin_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));

  auto args = grpc_core::CoreConfiguration::Get()
                  .channel_args_preconditioning()
                  .PreconditionChannelArgs(nullptr);
  grpc_error_handle error = grpc_tcp_server_create(
      &server->shutdown_complete,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
      server->on_connect, server->cb_data, &server->tcp_server);
  CHECK_OK(error);
  error =
      grpc_tcp_server_add_port(server->tcp_server, &resolved_addr, &port_added);
  CHECK_OK(error);
  CHECK(port_added == port);

  grpc_tcp_server_start(server->tcp_server, &server->pollset);
  LOG(INFO) << "test tcp server listening on 0.0.0.0:" << port;
}

void test_tcp_server_poll(test_tcp_server* server, int milliseconds) {
  grpc_pollset_worker* worker = nullptr;
  grpc_core::ExecCtx exec_ctx;
  grpc_core::Timestamp deadline = grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_milliseconds_to_deadline(milliseconds));
  gpr_mu_lock(server->mu);
  GRPC_LOG_IF_ERROR("pollset_work",
                    grpc_pollset_work(server->pollset[0], &worker, deadline));
  gpr_mu_unlock(server->mu);
}

static void do_nothing(void* /*arg*/, grpc_error_handle /*error*/) {}
static void finish_pollset(void* arg, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(arg));
}

void test_tcp_server_destroy(test_tcp_server* server) {
  grpc_core::ExecCtx exec_ctx;
  gpr_timespec shutdown_deadline;
  grpc_closure do_nothing_cb;
  grpc_tcp_server_unref(server->tcp_server);
  GRPC_CLOSURE_INIT(&do_nothing_cb, do_nothing, nullptr,
                    grpc_schedule_on_exec_ctx);
  shutdown_deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_seconds(5, GPR_TIMESPAN));
  grpc_core::ExecCtx::Get()->Flush();
  gpr_mu_lock(server->mu);
  while (!server->shutdown &&
         gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), shutdown_deadline) < 0) {
    gpr_mu_unlock(server->mu);
    test_tcp_server_poll(server, 100);
    gpr_mu_lock(server->mu);
  }
  gpr_mu_unlock(server->mu);
  grpc_pollset_shutdown(server->pollset[0],
                        GRPC_CLOSURE_CREATE(finish_pollset, server->pollset[0],
                                            grpc_schedule_on_exec_ctx));
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(server->pollset[0]);
  grpc_shutdown();
}
