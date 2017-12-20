/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

// This test won't work except with libuv
#ifdef GRPC_UV

#include <uv.h>

#include "src/core/lib/iomgr/tcp_server.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", #x)

static gpr_mu* g_mu;
static grpc_pollset* g_pollset;
static int g_nconnects = 0;

typedef struct on_connect_result {
  /* Owns a ref to server. */
  grpc_tcp_server* server;
  unsigned port_index;
  unsigned fd_index;
} on_connect_result;

typedef struct server_weak_ref {
  grpc_tcp_server* server;

  /* arg is this server_weak_ref. */
  grpc_closure server_shutdown;
} server_weak_ref;

static on_connect_result g_result = {NULL, 0, 0};

static void on_connect_result_init(on_connect_result* result) {
  result->server = NULL;
  result->port_index = 0;
  result->fd_index = 0;
}

static void on_connect_result_set(on_connect_result* result,
                                  const grpc_tcp_server_acceptor* acceptor) {
  result->server = grpc_tcp_server_ref(acceptor->from_server);
  result->port_index = acceptor->port_index;
  result->fd_index = acceptor->fd_index;
}

static void server_weak_ref_shutdown(void* arg, grpc_error* error) {
  server_weak_ref* weak_ref = static_cast<server_weak_ref*>(arg);
  weak_ref->server = NULL;
}

static void server_weak_ref_init(server_weak_ref* weak_ref) {
  weak_ref->server = NULL;
  GRPC_CLOSURE_INIT(&weak_ref->server_shutdown, server_weak_ref_shutdown,
                    weak_ref, grpc_schedule_on_exec_ctx);
}

/* Make weak_ref->server_shutdown a shutdown_starting cb on server.
   grpc_tcp_server promises that the server object will live until
   weak_ref->server_shutdown has returned. A strong ref on grpc_tcp_server
   should be held until server_weak_ref_set() returns to avoid a race where the
   server is deleted before the shutdown_starting cb is added. */
static void server_weak_ref_set(server_weak_ref* weak_ref,
                                grpc_tcp_server* server) {
  grpc_tcp_server_shutdown_starting_add(server, &weak_ref->server_shutdown);
  weak_ref->server = server;
}

static void on_connect(void* arg, grpc_endpoint* tcp, grpc_pollset* pollset,
                       grpc_tcp_server_acceptor* acceptor) {
  grpc_endpoint_shutdown(tcp,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Connected"));
  grpc_endpoint_destroy(tcp);

  on_connect_result temp_result;
  on_connect_result_set(&temp_result, acceptor);
  gpr_free(acceptor);

  gpr_mu_lock(g_mu);
  g_result = temp_result;
  g_nconnects++;
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, NULL)));
  gpr_mu_unlock(g_mu);
}

static void test_no_op(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(NULL, NULL, &s));
  grpc_tcp_server_unref(s);
}

static void test_no_op_with_start(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(NULL, NULL, &s));
  LOG_TEST("test_no_op_with_start");
  grpc_tcp_server_start(s, NULL, 0, on_connect, NULL);
  grpc_tcp_server_unref(s);
}

static void test_no_op_with_port(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr = (struct sockaddr_in*)resolved_addr.addr;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(NULL, NULL, &s));
  LOG_TEST("test_no_op_with_port");

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = sizeof(struct sockaddr_in);
  addr->sin_family = AF_INET;
  int port;
  GPR_ASSERT(grpc_tcp_server_add_port(s, &resolved_addr, &port) ==
                 GRPC_ERROR_NONE &&
             port > 0);

  grpc_tcp_server_unref(s);
}

static void test_no_op_with_port_and_start(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr = (struct sockaddr_in*)resolved_addr.addr;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(NULL, NULL, &s));
  LOG_TEST("test_no_op_with_port_and_start");
  int port;

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = sizeof(struct sockaddr_in);
  addr->sin_family = AF_INET;
  GPR_ASSERT(grpc_tcp_server_add_port(s, &resolved_addr, &port) ==
                 GRPC_ERROR_NONE &&
             port > 0);

  grpc_tcp_server_start(s, NULL, 0, on_connect, NULL);

  grpc_tcp_server_unref(s);
}

static void connect_cb(uv_connect_t* req, int status) {
  GPR_ASSERT(status == 0);
  gpr_free(req);
}

static void close_cb(uv_handle_t* handle) { gpr_free(handle); }

static void tcp_connect(const struct sockaddr* remote, socklen_t remote_len,
                        on_connect_result* result) {
  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(10);
  uv_tcp_t* client_handle =
      static_cast<uv_tcp_t*>(gpr_malloc(sizeof(uv_tcp_t)));
  uv_connect_t* req =
      static_cast<uv_connect_t*>(gpr_malloc(sizeof(uv_connect_t)));
  int nconnects_before;

  gpr_mu_lock(g_mu);
  nconnects_before = g_nconnects;
  on_connect_result_init(&g_result);
  GPR_ASSERT(uv_tcp_init(uv_default_loop(), client_handle) == 0);
  gpr_log(GPR_DEBUG, "start connect");
  GPR_ASSERT(uv_tcp_connect(req, client_handle, remote, connect_cb) == 0);
  gpr_log(GPR_DEBUG, "wait");
  while (g_nconnects == nconnects_before &&
         gpr_time_cmp(deadline, gpr_now(deadline.clock_type)) > 0) {
    grpc_pollset_worker* worker = NULL;
    GPR_ASSERT(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(g_pollset, &worker,
                          grpc_timespec_to_millis_round_up(deadline))));
    gpr_mu_unlock(g_mu);

    gpr_mu_lock(g_mu);
  }
  gpr_log(GPR_DEBUG, "wait done");
  GPR_ASSERT(g_nconnects == nconnects_before + 1);
  uv_close((uv_handle_t*)client_handle, close_cb);
  *result = g_result;

  gpr_mu_unlock(g_mu);
}

/* Tests a tcp server with multiple ports. */
static void test_connect(unsigned n) {
  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  grpc_resolved_address resolved_addr1;
  struct sockaddr_storage* addr = (struct sockaddr_storage*)resolved_addr.addr;
  struct sockaddr_storage* addr1 =
      (struct sockaddr_storage*)resolved_addr1.addr;
  int svr_port;
  int svr1_port;
  grpc_tcp_server* s;
  GPR_ASSERT(GRPC_ERROR_NONE == grpc_tcp_server_create(NULL, NULL, &s));
  unsigned i;
  server_weak_ref weak_ref;
  server_weak_ref_init(&weak_ref);
  LOG_TEST("test_connect");
  gpr_log(GPR_INFO, "clients=%d", n);
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  memset(&resolved_addr1, 0, sizeof(resolved_addr1));
  resolved_addr.len = sizeof(struct sockaddr_storage);
  resolved_addr1.len = sizeof(struct sockaddr_storage);
  addr->ss_family = addr1->ss_family = AF_INET;
  GPR_ASSERT(GRPC_ERROR_NONE ==
             grpc_tcp_server_add_port(s, &resolved_addr, &svr_port));
  GPR_ASSERT(svr_port > 0);
  GPR_ASSERT((uv_ip6_addr("::", svr_port, (struct sockaddr_in6*)addr)) == 0);
  /* Cannot use wildcard (port==0), because add_port() will try to reuse the
     same port as a previous add_port(). */
  svr1_port = grpc_pick_unused_port_or_die();
  grpc_sockaddr_set_port(&resolved_addr1, svr1_port);
  GPR_ASSERT(grpc_tcp_server_add_port(s, &resolved_addr1, &svr_port) ==
                 GRPC_ERROR_NONE &&
             svr_port == svr1_port);

  grpc_tcp_server_start(s, &g_pollset, 1, on_connect, NULL);

  GPR_ASSERT(uv_ip6_addr("::", svr_port, (struct sockaddr_in6*)addr1) == 0);

  for (i = 0; i < n; i++) {
    on_connect_result result;
    on_connect_result_init(&result);
    tcp_connect((struct sockaddr*)addr, (socklen_t)resolved_addr.len, &result);
    GPR_ASSERT(result.port_index == 0);
    GPR_ASSERT(result.server == s);
    if (weak_ref.server == NULL) {
      server_weak_ref_set(&weak_ref, result.server);
    }
    grpc_tcp_server_unref(result.server);

    on_connect_result_init(&result);
    tcp_connect((struct sockaddr*)addr1, (socklen_t)resolved_addr1.len,
                &result);
    GPR_ASSERT(result.port_index == 1);
    GPR_ASSERT(result.server == s);
    grpc_tcp_server_unref(result.server);
  }

  /* Weak ref to server valid until final unref. */
  GPR_ASSERT(weak_ref.server != NULL);

  grpc_tcp_server_unref(s);

  /* Weak ref lost. */
  GPR_ASSERT(weak_ref.server == NULL);
}

static void destroy_pollset(void* p, grpc_error* error) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

int main(int argc, char** argv) {
  grpc_closure destroyed;
  grpc_core::ExecCtx exec_ctx;
  grpc_test_init(argc, argv);
  grpc_init();
  g_pollset = static_cast<grpc_pollset*>(gpr_malloc(grpc_pollset_size()));
  grpc_pollset_init(g_pollset, &g_mu);

  test_no_op();
  test_no_op_with_start();
  test_no_op_with_port();
  test_no_op_with_port_and_start();
  test_connect(1);
  test_connect(10);

  GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                    grpc_schedule_on_exec_ctx);
  grpc_pollset_shutdown(g_pollset, &destroyed);

  grpc_shutdown();
  gpr_free(g_pollset);
  return 0;
}

#else /* GRPC_UV */

int main(int argc, char** argv) { return 1; }

#endif /* GRPC_UV */
