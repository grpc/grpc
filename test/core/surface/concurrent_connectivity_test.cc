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

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args_preconditioning.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/util/thd.h"
#include "src/core/util/time.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

// TODO(yashykt): When our macos testing infrastructure becomes good enough, we
// wouldn't need to reduce the number of threads on MacOS
#ifdef __APPLE__
#define NUM_THREADS 10
#else
#define NUM_THREADS 100
#endif  // __APPLE

#define NUM_OUTER_LOOPS 10
#define NUM_INNER_LOOPS 10
#define DELAY_MILLIS 10
#define POLL_MILLIS 15000

#define NUM_OUTER_LOOPS_SHORT_TIMEOUTS 10
#define NUM_INNER_LOOPS_SHORT_TIMEOUTS 100
#define DELAY_MILLIS_SHORT_TIMEOUTS 1
// in a successful test run, POLL_MILLIS should never be reached because all
// runs should end after the shorter delay_millis
#define POLL_MILLIS_SHORT_TIMEOUTS 30000
// it should never take longer that this to shutdown the server
#define SERVER_SHUTDOWN_TIMEOUT 30000

static void* tag(int n) { return reinterpret_cast<void*>(n); }

void create_loop_destroy(void* addr) {
  for (int i = 0; i < NUM_OUTER_LOOPS; ++i) {
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    grpc_channel* chan =
        grpc_channel_create(static_cast<char*>(addr), creds, nullptr);
    grpc_channel_credentials_release(creds);

    for (int j = 0; j < NUM_INNER_LOOPS; ++j) {
      gpr_timespec later_time =
          grpc_timeout_milliseconds_to_deadline(DELAY_MILLIS);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(chan, 1);
      grpc_channel_watch_connectivity_state(chan, state, later_time, cq,
                                            nullptr);
      gpr_timespec poll_time =
          grpc_timeout_milliseconds_to_deadline(POLL_MILLIS);
      ASSERT_EQ(grpc_completion_queue_next(cq, poll_time, nullptr).type,
                GRPC_OP_COMPLETE);
    }
    grpc_channel_destroy(chan);
    grpc_completion_queue_destroy(cq);
  }
}

// Always stack-allocate or new ServerThreadArgs; never use gpr_malloc since
// this contains C++ objects.
struct ServerThreadArgs {
  std::string addr;
  grpc_server* server = nullptr;
  grpc_completion_queue* cq = nullptr;
  std::vector<grpc_pollset*> pollset;
  gpr_mu* mu = nullptr;
  gpr_event ready;
  std::atomic_bool stop{false};
};

void server_thread(void* vargs) {
  struct ServerThreadArgs* args = static_cast<struct ServerThreadArgs*>(vargs);
  grpc_event ev;
  gpr_timespec deadline =
      grpc_timeout_milliseconds_to_deadline(SERVER_SHUTDOWN_TIMEOUT);
  ev = grpc_completion_queue_next(args->cq, deadline, nullptr);
  ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
  ASSERT_EQ(ev.tag, tag(0xd1e));
}

static void on_connect(void* vargs, grpc_endpoint* tcp,
                       grpc_pollset* /*accepting_pollset*/,
                       grpc_tcp_server_acceptor* acceptor) {
  gpr_free(acceptor);
  struct ServerThreadArgs* args = static_cast<struct ServerThreadArgs*>(vargs);
  grpc_endpoint_destroy(tcp);
  gpr_mu_lock(args->mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(args->pollset[0], nullptr));
  gpr_mu_unlock(args->mu);
}

void bad_server_thread(void* vargs) {
  struct ServerThreadArgs* args = static_cast<struct ServerThreadArgs*>(vargs);

  grpc_core::ExecCtx exec_ctx;
  grpc_resolved_address resolved_addr;
  grpc_sockaddr* addr = reinterpret_cast<grpc_sockaddr*>(resolved_addr.addr);
  int port;
  grpc_tcp_server* s;
  auto channel_args = grpc_core::CoreConfiguration::Get()
                          .channel_args_preconditioning()
                          .PreconditionChannelArgs(nullptr);
  grpc_error_handle error = grpc_tcp_server_create(
      nullptr,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(channel_args),
      on_connect, args, &s);
  ASSERT_TRUE(error.ok());
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  addr->sa_family = GRPC_AF_INET;
  resolved_addr.len = sizeof(grpc_sockaddr_in);
  error = grpc_tcp_server_add_port(s, &resolved_addr, &port);
  ASSERT_TRUE(GRPC_LOG_IF_ERROR("grpc_tcp_server_add_port", error));
  ASSERT_GT(port, 0);
  args->addr = absl::StrCat("localhost:", port);

  grpc_tcp_server_start(s, &args->pollset);
  gpr_event_set(&args->ready, reinterpret_cast<void*>(1));

  gpr_mu_lock(args->mu);
  while (!args->stop.load(std::memory_order_acquire)) {
    grpc_core::Timestamp deadline =
        grpc_core::Timestamp::Now() + grpc_core::Duration::Milliseconds(100);

    grpc_pollset_worker* worker = nullptr;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(args->pollset[0], &worker, deadline))) {
      args->stop.store(true, std::memory_order_release);
    }
    gpr_mu_unlock(args->mu);

    gpr_mu_lock(args->mu);
  }
  gpr_mu_unlock(args->mu);

  grpc_tcp_server_unref(s);
}

static void done_pollset_shutdown(void* pollset, grpc_error_handle /*error*/) {
  grpc_pollset_destroy(static_cast<grpc_pollset*>(pollset));
  gpr_free(pollset);
}

TEST(ConcurrentConnectivityTest, RunConcurrentConnectivityTest) {
  struct ServerThreadArgs args;

  // First round, no server
  {
    VLOG(2) << "Wave 1";
    grpc_core::Thread threads[NUM_THREADS];
    args.addr = "localhost:54321";
    for (auto& th : threads) {
      th = grpc_core::Thread("grpc_wave_1", create_loop_destroy,
                             const_cast<char*>(args.addr.c_str()));
      th.Start();
    }
    for (auto& th : threads) {
      th.Join();
    }
  }

  // Second round, actual grpc server
  {
    VLOG(2) << "Wave 2";
    int port = grpc_pick_unused_port_or_die();
    args.addr = absl::StrCat("localhost:", port);
    args.server = grpc_server_create(nullptr, nullptr);
    grpc_server_credentials* server_creds =
        grpc_insecure_server_credentials_create();
    grpc_server_add_http2_port(args.server, args.addr.c_str(), server_creds);
    grpc_server_credentials_release(server_creds);
    args.cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_server_register_completion_queue(args.server, args.cq, nullptr);
    grpc_server_start(args.server);
    grpc_core::Thread server2("grpc_wave_2_server", server_thread, &args);
    server2.Start();

    grpc_core::Thread threads[NUM_THREADS];
    for (auto& th : threads) {
      th = grpc_core::Thread("grpc_wave_2", create_loop_destroy,
                             const_cast<char*>(args.addr.c_str()));
      th.Start();
    }
    for (auto& th : threads) {
      th.Join();
    }
    grpc_server_shutdown_and_notify(args.server, args.cq, tag(0xd1e));

    server2.Join();
    grpc_server_destroy(args.server);
    grpc_completion_queue_destroy(args.cq);
  }

  // Third round, bogus tcp server
  {
    VLOG(2) << "Wave 3";
    auto* pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
    grpc_pollset_init(pollset, &args.mu);
    args.pollset.push_back(pollset);
    gpr_event_init(&args.ready);
    grpc_core::Thread server3("grpc_wave_3_server", bad_server_thread, &args);
    server3.Start();
    gpr_event_wait(&args.ready, gpr_inf_future(GPR_CLOCK_MONOTONIC));

    grpc_core::Thread threads[NUM_THREADS];
    for (auto& th : threads) {
      th = grpc_core::Thread("grpc_wave_3", create_loop_destroy,
                             const_cast<char*>(args.addr.c_str()));
      th.Start();
    }
    for (auto& th : threads) {
      th.Join();
    }

    args.stop.store(true, std::memory_order_release);
    server3.Join();
    {
      grpc_core::ExecCtx exec_ctx;
      grpc_pollset_shutdown(
          args.pollset[0],
          GRPC_CLOSURE_CREATE(done_pollset_shutdown, args.pollset[0],
                              grpc_schedule_on_exec_ctx));
    }
  }
}

void watches_with_short_timeouts(void* addr) {
  for (int i = 0; i < NUM_OUTER_LOOPS_SHORT_TIMEOUTS; ++i) {
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel_credentials* creds = grpc_insecure_credentials_create();
    grpc_channel* chan =
        grpc_channel_create(static_cast<char*>(addr), creds, nullptr);
    grpc_channel_credentials_release(creds);

    for (int j = 0; j < NUM_INNER_LOOPS_SHORT_TIMEOUTS; ++j) {
      gpr_timespec later_time =
          grpc_timeout_milliseconds_to_deadline(DELAY_MILLIS_SHORT_TIMEOUTS);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(chan, 0);
      ASSERT_EQ(state, GRPC_CHANNEL_IDLE);
      grpc_channel_watch_connectivity_state(chan, state, later_time, cq,
                                            nullptr);
      gpr_timespec poll_time =
          grpc_timeout_milliseconds_to_deadline(POLL_MILLIS_SHORT_TIMEOUTS);
      grpc_event ev = grpc_completion_queue_next(cq, poll_time, nullptr);
      ASSERT_EQ(ev.type, GRPC_OP_COMPLETE);
      ASSERT_EQ(ev.success, false);
    }
    grpc_channel_destroy(chan);
    grpc_completion_queue_destroy(cq);
  }
}

// This test tries to catch deadlock situations.
// With short timeouts on "watches" and long timeouts on cq next calls,
// so that a QUEUE_TIMEOUT likely means that something is stuck.
TEST(ConcurrentConnectivityTest, RunConcurrentWatchesWithShortTimeoutsTest) {
  grpc_core::Thread threads[NUM_THREADS];
  for (auto& th : threads) {
    th = grpc_core::Thread("grpc_short_watches", watches_with_short_timeouts,
                           const_cast<char*>("localhost:54321"));
    th.Start();
  }
  for (auto& th : threads) {
    th.Join();
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
