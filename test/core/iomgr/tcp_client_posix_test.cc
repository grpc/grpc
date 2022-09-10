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

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/util/test_config.h"

// This test won't work except with posix sockets enabled
#ifdef GRPC_POSIX_SOCKET_TCP_CLIENT

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/resource_quota/api.h"

static grpc_pollset_set* g_pollset_set;
static gpr_mu* g_mu;
static grpc_pollset* g_pollset;
static int g_connections_complete = 0;
static grpc_endpoint* g_connecting = nullptr;

static grpc_core::Timestamp test_deadline(void) {
  return grpc_core::Timestamp::FromTimespecRoundUp(
      grpc_timeout_seconds_to_deadline(10));
}

static void finish_connection() {
  gpr_mu_lock(g_mu);
  g_connections_complete++;
  grpc_core::ExecCtx exec_ctx;
  ASSERT_TRUE(
      GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(g_pollset, nullptr)));

  gpr_mu_unlock(g_mu);
}

static void must_succeed(void* /*arg*/, grpc_error_handle error) {
  ASSERT_NE(g_connecting, nullptr);
  ASSERT_TRUE(GRPC_ERROR_IS_NONE(error));
  grpc_endpoint_shutdown(g_connecting, GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                                           "must_succeed called"));
  grpc_endpoint_destroy(g_connecting);
  g_connecting = nullptr;
  finish_connection();
}

static void must_fail(void* /*arg*/, grpc_error_handle error) {
  ASSERT_EQ(g_connecting, nullptr);
  ASSERT_FALSE(GRPC_ERROR_IS_NONE(error));
  finish_connection();
}

void test_succeeds(void) {
  gpr_log(GPR_ERROR, "---- starting test_succeeds() ----");
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  int svr_fd;
  int r;
  int connections_complete_before;
  grpc_closure done;
  grpc_core::ExecCtx exec_ctx;

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;

  /* create a phony server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(svr_fd, 0);
  ASSERT_EQ(bind(svr_fd, (struct sockaddr*)addr, (socklen_t)resolved_addr.len),
            0);
  ASSERT_EQ(listen(svr_fd, 1), 0);

  gpr_mu_lock(g_mu);
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(g_mu);

  /* connect to it */
  ASSERT_EQ(getsockname(svr_fd, (struct sockaddr*)addr,
                        (socklen_t*)&resolved_addr.len),
            0);
  GRPC_CLOSURE_INIT(&done, must_succeed, nullptr, grpc_schedule_on_exec_ctx);
  grpc_core::ChannelArgs args = grpc_core::CoreConfiguration::Get()
                                    .channel_args_preconditioning()
                                    .PreconditionChannelArgs(nullptr);
  int64_t connection_handle = grpc_tcp_client_connect(
      &done, &g_connecting, g_pollset_set,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
      &resolved_addr, grpc_core::Timestamp::InfFuture());
  /* await the connection */
  do {
    resolved_addr.len = static_cast<socklen_t>(sizeof(addr));
    r = accept(svr_fd, reinterpret_cast<struct sockaddr*>(addr),
               reinterpret_cast<socklen_t*>(&resolved_addr.len));
  } while (r == -1 && errno == EINTR);
  ASSERT_GE(r, 0);
  close(r);

  gpr_mu_lock(g_mu);

  while (g_connections_complete == connections_complete_before) {
    grpc_pollset_worker* worker = nullptr;
    ASSERT_TRUE(GRPC_LOG_IF_ERROR(
        "pollset_work",
        grpc_pollset_work(g_pollset, &worker,
                          grpc_core::Timestamp::FromTimespecRoundUp(
                              grpc_timeout_seconds_to_deadline(5)))));
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }

  gpr_mu_unlock(g_mu);

  // A cancellation attempt should fail because connect already succeeded.
  ASSERT_EQ(grpc_tcp_client_cancel_connect(connection_handle), false);

  gpr_log(GPR_ERROR, "---- finished test_succeeds() ----");
}

void test_fails(void) {
  gpr_log(GPR_ERROR, "---- starting test_fails() ----");
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  int connections_complete_before;
  grpc_closure done;
  grpc_core::ExecCtx exec_ctx;

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;

  gpr_mu_lock(g_mu);
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(g_mu);

  /* connect to a broken address */
  GRPC_CLOSURE_INIT(&done, must_fail, nullptr, grpc_schedule_on_exec_ctx);
  int64_t connection_handle = grpc_tcp_client_connect(
      &done, &g_connecting, g_pollset_set,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(),
      &resolved_addr, grpc_core::Timestamp::InfFuture());
  gpr_mu_lock(g_mu);

  /* wait for the connection callback to finish */
  while (g_connections_complete == connections_complete_before) {
    grpc_pollset_worker* worker = nullptr;
    grpc_core::Timestamp polling_deadline = test_deadline();
    switch (grpc_timer_check(&polling_deadline)) {
      case GRPC_TIMERS_FIRED:
        break;
      case GRPC_TIMERS_NOT_CHECKED:
        polling_deadline = grpc_core::Timestamp::ProcessEpoch();
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_TIMERS_CHECKED_AND_EMPTY:
        ASSERT_TRUE(GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(g_pollset, &worker, polling_deadline)));
        break;
    }
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }

  gpr_mu_unlock(g_mu);

  // A cancellation attempt should fail because connect already failed.
  ASSERT_EQ(grpc_tcp_client_cancel_connect(connection_handle), false);

  gpr_log(GPR_ERROR, "---- finished test_fails() ----");
}

void test_connect_cancellation_succeeds(void) {
  gpr_log(GPR_ERROR, "---- starting test_connect_cancellation_succeeds() ----");
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  int svr_fd;
  grpc_closure done;
  grpc_core::ExecCtx exec_ctx;

  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;

  /* create a phony server */
  svr_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(svr_fd, 0);
  ASSERT_EQ(bind(svr_fd, (struct sockaddr*)addr, (socklen_t)resolved_addr.len),
            0);
  ASSERT_EQ(listen(svr_fd, 1), 0);

  // connect to it. accept() is not called on the bind socket. So the connection
  // should appear to be stuck giving ample time to try to cancel it.
  ASSERT_EQ(getsockname(svr_fd, (struct sockaddr*)addr,
                        (socklen_t*)&resolved_addr.len),
            0);
  GRPC_CLOSURE_INIT(&done, must_succeed, nullptr, grpc_schedule_on_exec_ctx);
  grpc_core::ChannelArgs args = grpc_core::CoreConfiguration::Get()
                                    .channel_args_preconditioning()
                                    .PreconditionChannelArgs(nullptr);
  int64_t connection_handle = grpc_tcp_client_connect(
      &done, &g_connecting, g_pollset_set,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(args),
      &resolved_addr, grpc_core::Timestamp::InfFuture());
  ASSERT_GT(connection_handle, 0);
  ASSERT_EQ(grpc_tcp_client_cancel_connect(connection_handle), true);
  close(svr_fd);
  gpr_log(GPR_ERROR, "---- finished test_connect_cancellation_succeeds() ----");
}

void test_fails_bad_addr_no_leak(void) {
  gpr_log(GPR_ERROR, "---- starting test_fails_bad_addr_no_leak() ----");
  grpc_resolved_address resolved_addr;
  struct sockaddr_in* addr =
      reinterpret_cast<struct sockaddr_in*>(resolved_addr.addr);
  int connections_complete_before;
  grpc_closure done;
  grpc_core::ExecCtx exec_ctx;
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  resolved_addr.len = static_cast<socklen_t>(sizeof(struct sockaddr_in));
  // force `grpc_tcp_client_prepare_fd` to fail. contrived, but effective.
  addr->sin_family = AF_IPX;
  gpr_mu_lock(g_mu);
  connections_complete_before = g_connections_complete;
  gpr_mu_unlock(g_mu);
  // connect to an invalid address.
  GRPC_CLOSURE_INIT(&done, must_fail, nullptr, grpc_schedule_on_exec_ctx);
  grpc_tcp_client_connect(
      &done, &g_connecting, g_pollset_set,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(),
      &resolved_addr, grpc_core::Timestamp::InfFuture());
  gpr_mu_lock(g_mu);
  while (g_connections_complete == connections_complete_before) {
    grpc_pollset_worker* worker = nullptr;
    grpc_core::Timestamp polling_deadline = test_deadline();
    switch (grpc_timer_check(&polling_deadline)) {
      case GRPC_TIMERS_FIRED:
        break;
      case GRPC_TIMERS_NOT_CHECKED:
        polling_deadline = grpc_core::Timestamp::ProcessEpoch();
        ABSL_FALLTHROUGH_INTENDED;
      case GRPC_TIMERS_CHECKED_AND_EMPTY:
        ASSERT_TRUE(GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(g_pollset, &worker, polling_deadline)));
        break;
    }
    gpr_mu_unlock(g_mu);
    grpc_core::ExecCtx::Get()->Flush();
    gpr_mu_lock(g_mu);
  }
  gpr_mu_unlock(g_mu);
  gpr_log(GPR_ERROR, "---- finished test_fails_bad_addr_no_leak() ----");
}

static void destroy_pollset(void* p, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(p));
}

TEST(TcpClientPosixTest, MainTest) {
  grpc_closure destroyed;
  grpc_init();

  {
    grpc_core::ExecCtx exec_ctx;
    g_pollset_set = grpc_pollset_set_create();
    g_pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(g_pollset, &g_mu);
    grpc_pollset_set_add_pollset(g_pollset_set, g_pollset);

    test_succeeds();
    test_connect_cancellation_succeeds();
    test_fails();
    test_fails_bad_addr_no_leak();
    grpc_pollset_set_destroy(g_pollset_set);
    GRPC_CLOSURE_INIT(&destroyed, destroy_pollset, g_pollset,
                      grpc_schedule_on_exec_ctx);
    grpc_pollset_shutdown(g_pollset, &destroyed);
  }

  grpc_shutdown();
  gpr_free(g_pollset);
}

#endif /* GRPC_POSIX_SOCKET_CLIENT */

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
