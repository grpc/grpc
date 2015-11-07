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

#include "test/core/util/test_tcp_server.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>
#include "src/core/iomgr/endpoint.h"
#include "src/core/iomgr/sockaddr.h"
#include "src/core/iomgr/tcp_server.h"
#include "test/core/util/port.h"

void test_tcp_server_init(test_tcp_server *server,
                          grpc_tcp_server_cb on_connect, void *user_data) {
  grpc_init();
  server->tcp_server = NULL;
  grpc_pollset_init(&server->pollset);
  server->pollsets[0] = &server->pollset;
  server->on_connect = on_connect;
  server->cb_data = user_data;
}

void test_tcp_server_start(test_tcp_server *server, int port) {
  struct sockaddr_in addr;
  int port_added;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  addr.sin_family = AF_INET;
  addr.sin_port = htons((gpr_uint16)port);
  memset(&addr.sin_addr, 0, sizeof(addr.sin_addr));

  server->tcp_server = grpc_tcp_server_create();
  port_added =
      grpc_tcp_server_add_port(server->tcp_server, &addr, sizeof(addr));
  GPR_ASSERT(port_added == port);

  grpc_tcp_server_start(&exec_ctx, server->tcp_server, server->pollsets, 1,
                        server->on_connect, server->cb_data);
  gpr_log(GPR_INFO, "test tcp server listening on 0.0.0.0:%d", port);

  grpc_exec_ctx_finish(&exec_ctx);
}

void test_tcp_server_poll(test_tcp_server *server, int seconds) {
  grpc_pollset_worker worker;
  gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                   gpr_time_from_seconds(seconds, GPR_TIMESPAN));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  gpr_mu_lock(GRPC_POLLSET_MU(&server->pollset));
  grpc_pollset_work(&exec_ctx, &server->pollset, &worker,
                    gpr_now(GPR_CLOCK_MONOTONIC), deadline);
  gpr_mu_unlock(GRPC_POLLSET_MU(&server->pollset));
  grpc_exec_ctx_finish(&exec_ctx);
}

static void do_nothing(grpc_exec_ctx *exec_ctx, void *ignored, int success) {}

void test_tcp_server_destroy(test_tcp_server *server) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_closure do_nothing_closure[2];
  grpc_closure_init(&do_nothing_closure[0], do_nothing, NULL);
  grpc_closure_init(&do_nothing_closure[1], do_nothing, NULL);
  grpc_tcp_server_destroy(&exec_ctx, server->tcp_server,
                          &do_nothing_closure[0]);
  grpc_pollset_shutdown(&exec_ctx, &server->pollset, &do_nothing_closure[1]);
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_pollset_destroy(&server->pollset);
  grpc_shutdown();
}
