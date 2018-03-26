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

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include "test/core/util/test_tcp_server.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

static void on_server_destroyed(void* data, grpc_error* error) {
  test_tcp_server* server = static_cast<test_tcp_server*>(data);
  server->shutdown = 1;
}

void test_tcp_server_init(test_tcp_server* server,
                          grpc_tcp_server_cb on_connect, void* user_data) {
  grpc_init();
  server->tcp_server = nullptr;
  GRPC_CLOSURE_INIT(&server->shutdown_complete, on_server_destroyed, server,
                    grpc_schedule_on_exec_ctx);
  server->shutdown = 0;
  server->pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(server->pollset, &server->mu);
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

  grpc_error* error = grpc_tcp_server_create(&server->shutdown_complete,
                                             nullptr, &server->tcp_server);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  error =
      grpc_tcp_server_add_port(server->tcp_server, &resolved_addr, &port_added);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(port_added == port);

  grpc_tcp_server_start(server->tcp_server, &server->pollset, 1,
                        server->on_connect, server->cb_data);
  gpr_log(GPR_INFO, "test tcp server listening on 0.0.0.0:%d", port);
}

void test_tcp_server_poll(test_tcp_server* server, int seconds) {
  grpc_pollset_worker* worker = nullptr;
  grpc_core::ExecCtx exec_ctx;
  grpc_millis deadline = grpc_timespec_to_millis_round_up(
      grpc_timeout_seconds_to_deadline(seconds));
  gpr_mu_lock(server->mu);
  GRPC_LOG_IF_ERROR("pollset_work",
                    grpc_pollset_work(server->pollset, &worker, deadline));
  gpr_mu_unlock(server->mu);
}

static void do_nothing(void* arg, grpc_error* error) {}
static void finish_pollset(void* arg, grpc_error* error) {
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
  while (!server->shutdown &&
         gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), shutdown_deadline) < 0) {
    test_tcp_server_poll(server, 1);
  }
  grpc_pollset_shutdown(server->pollset,
                        GRPC_CLOSURE_CREATE(finish_pollset, server->pollset,
                                            grpc_schedule_on_exec_ctx));
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(server->pollset);
  grpc_shutdown();
}
