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

#include "test/core/util/test_tcp_server.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "test/core/util/port.h"

static void on_server_destroyed(grpc_exec_ctx *exec_ctx, void *data,
                                grpc_error *error) {
  test_tcp_server *server = data;
  server->shutdown = 1;
}

void test_tcp_server_init(test_tcp_server *server,
                          grpc_tcp_server_cb on_connect, void *user_data) {
  grpc_init();
  server->tcp_server = NULL;
  GRPC_CLOSURE_INIT(&server->shutdown_complete, on_server_destroyed, server,
                    grpc_schedule_on_exec_ctx);
  server->shutdown = 0;
  server->pollset = gpr_zalloc(grpc_pollset_size());
  grpc_pollset_init(server->pollset, &server->mu);
  server->on_connect = on_connect;
  server->cb_data = user_data;
}

void test_tcp_server_start(test_tcp_server *server, int port) {
  grpc_resolved_address resolved_addr;
  struct sockaddr_in *addr = (struct sockaddr_in *)resolved_addr.addr;
  int port_added;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  addr->sin_family = AF_INET;
  addr->sin_port = htons((uint16_t)port);
  memset(&addr->sin_addr, 0, sizeof(addr->sin_addr));

  grpc_error *error = grpc_tcp_server_create(
      &exec_ctx, &server->shutdown_complete, NULL, &server->tcp_server);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  error =
      grpc_tcp_server_add_port(server->tcp_server, &resolved_addr, &port_added);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(port_added == port);

  grpc_tcp_server_start(&exec_ctx, server->tcp_server, &server->pollset, 1,
                        server->on_connect, server->cb_data);
  gpr_log(GPR_INFO, "test tcp server listening on 0.0.0.0:%d", port);

  grpc_exec_ctx_finish(&exec_ctx);
}

void test_tcp_server_poll(test_tcp_server *server, int seconds) {
  grpc_pollset_worker *worker = NULL;
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_seconds(seconds, GPR_TIMESPAN));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_mu_lock(server->mu);
  GRPC_LOG_IF_ERROR("pollset_work",
                    grpc_pollset_work(&exec_ctx, server->pollset, &worker,
                                      gpr_now(GPR_CLOCK_MONOTONIC), deadline));
  gpr_mu_unlock(server->mu);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}
static void finish_pollset(grpc_exec_ctx *exec_ctx, void *arg,
                           grpc_error *error) {
  grpc_pollset_destroy(exec_ctx, arg);
}

void test_tcp_server_destroy(test_tcp_server *server) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_timespec shutdown_deadline;
  grpc_closure do_nothing_cb;
  grpc_tcp_server_unref(&exec_ctx, server->tcp_server);
  GRPC_CLOSURE_INIT(&do_nothing_cb, do_nothing, NULL,
                    grpc_schedule_on_exec_ctx);
  shutdown_deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                   gpr_time_from_seconds(5, GPR_TIMESPAN));
  while (!server->shutdown &&
         gpr_time_cmp(gpr_now(GPR_CLOCK_MONOTONIC), shutdown_deadline) < 0) {
    test_tcp_server_poll(server, 1);
  }
  grpc_pollset_shutdown(&exec_ctx, server->pollset,
                        GRPC_CLOSURE_CREATE(finish_pollset, server->pollset,
                                            grpc_schedule_on_exec_ctx));
  grpc_exec_ctx_finish(&exec_ctx);
  gpr_free(server->pollset);
  grpc_shutdown();
}
