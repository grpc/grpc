/*
 *
 * Copyright 2016 gRPC authors.
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

/* With the addition of a libuv endpoint, sockaddr.h now includes uv.h when
   using that endpoint. Because of various transitive includes in uv.h,
   including windows.h on Windows, uv.h must be included before other system
   headers. Therefore, sockaddr.h must always be included first */
#include "src/core/lib/iomgr/sockaddr.h"

#include <memory.h>
#include <stdio.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_server.h"

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#define NUM_THREADS 100
#define NUM_OUTER_LOOPS 10
#define NUM_INNER_LOOPS 10
#define DELAY_MILLIS 10
#define POLL_MILLIS 3000

#define NUM_OUTER_LOOPS_SHORT_TIMEOUTS 10
#define NUM_INNER_LOOPS_SHORT_TIMEOUTS 100
#define DELAY_MILLIS_SHORT_TIMEOUTS 1
// in a successful test run, POLL_MILLIS should never be reached beause all runs
// should
// end after the shorter delay_millis
#define POLL_MILLIS_SHORT_TIMEOUTS 30000

static void* tag(int n) { return (void*)(uintptr_t)n; }
static int detag(void* p) { return (int)(uintptr_t)p; }

void create_loop_destroy(void* addr) {
  for (int i = 0; i < NUM_OUTER_LOOPS; ++i) {
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel* chan =
        grpc_insecure_channel_create((char*)addr, nullptr, nullptr);

    for (int j = 0; j < NUM_INNER_LOOPS; ++j) {
      gpr_timespec later_time =
          grpc_timeout_milliseconds_to_deadline(DELAY_MILLIS);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(chan, 1);
      grpc_channel_watch_connectivity_state(chan, state, later_time, cq,
                                            nullptr);
      gpr_timespec poll_time =
          grpc_timeout_milliseconds_to_deadline(POLL_MILLIS);
      GPR_ASSERT(grpc_completion_queue_next(cq, poll_time, nullptr).type ==
                 GRPC_OP_COMPLETE);
      /* check that the watcher from "watch state" was free'd */
      GPR_ASSERT(grpc_channel_num_external_connectivity_watchers(chan) == 0);
    }
    grpc_channel_destroy(chan);
    grpc_completion_queue_destroy(cq);
  }
}

struct server_thread_args {
  char* addr;
  grpc_server* server;
  grpc_completion_queue* cq;
  grpc_pollset* pollset;
  gpr_mu* mu;
  gpr_event ready;
  gpr_atm stop;
};

void server_thread(void* vargs) {
  struct server_thread_args* args = (struct server_thread_args*)vargs;
  grpc_event ev;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ev = grpc_completion_queue_next(args->cq, deadline, nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(detag(ev.tag) == 0xd1e);
}

static void on_connect(grpc_exec_ctx* exec_ctx, void* vargs, grpc_endpoint* tcp,
                       grpc_pollset* accepting_pollset,
                       grpc_tcp_server_acceptor* acceptor) {
  gpr_free(acceptor);
  struct server_thread_args* args = (struct server_thread_args*)vargs;
  grpc_endpoint_shutdown(exec_ctx, tcp,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Connected"));
  grpc_endpoint_destroy(exec_ctx, tcp);
  gpr_mu_lock(args->mu);
  GRPC_LOG_IF_ERROR("pollset_kick",
                    grpc_pollset_kick(exec_ctx, args->pollset, nullptr));
  gpr_mu_unlock(args->mu);
}

void bad_server_thread(void* vargs) {
  struct server_thread_args* args = (struct server_thread_args*)vargs;

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resolved_address resolved_addr;
  struct sockaddr_storage* addr = (struct sockaddr_storage*)resolved_addr.addr;
  int port;
  grpc_tcp_server* s;
  grpc_error* error = grpc_tcp_server_create(&exec_ctx, nullptr, nullptr, &s);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  addr->ss_family = AF_INET;
  error = grpc_tcp_server_add_port(s, &resolved_addr, &port);
  GPR_ASSERT(GRPC_LOG_IF_ERROR("grpc_tcp_server_add_port", error));
  GPR_ASSERT(port > 0);
  gpr_asprintf(&args->addr, "localhost:%d", port);

  grpc_tcp_server_start(&exec_ctx, s, &args->pollset, 1, on_connect, args);
  gpr_event_set(&args->ready, (void*)1);

  gpr_mu_lock(args->mu);
  while (gpr_atm_acq_load(&args->stop) == 0) {
    grpc_millis deadline = grpc_exec_ctx_now(&exec_ctx) + 100;

    grpc_pollset_worker* worker = nullptr;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(&exec_ctx, args->pollset, &worker, deadline))) {
      gpr_atm_rel_store(&args->stop, 1);
    }
    gpr_mu_unlock(args->mu);
    grpc_exec_ctx_finish(&exec_ctx);
    gpr_mu_lock(args->mu);
  }
  gpr_mu_unlock(args->mu);

  grpc_tcp_server_unref(&exec_ctx, s);

  grpc_exec_ctx_finish(&exec_ctx);

  gpr_free(args->addr);
}

static void done_pollset_shutdown(grpc_exec_ctx* exec_ctx, void* pollset,
                                  grpc_error* error) {
  grpc_pollset_destroy(exec_ctx, static_cast<grpc_pollset*>(pollset));
  gpr_free(pollset);
}

int run_concurrent_connectivity_test() {
  struct server_thread_args args;
  memset(&args, 0, sizeof(args));

  grpc_init();

  gpr_thd_id threads[NUM_THREADS];
  gpr_thd_id server;

  char* localhost = gpr_strdup("localhost:54321");
  gpr_thd_options options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&options);

  /* First round, no server */
  gpr_log(GPR_DEBUG, "Wave 1");
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_new(&threads[i], create_loop_destroy, localhost, &options);
  }
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_join(threads[i]);
  }
  gpr_free(localhost);

  /* Second round, actual grpc server */
  gpr_log(GPR_DEBUG, "Wave 2");
  int port = grpc_pick_unused_port_or_die();
  gpr_asprintf(&args.addr, "localhost:%d", port);
  args.server = grpc_server_create(nullptr, nullptr);
  grpc_server_add_insecure_http2_port(args.server, args.addr);
  args.cq = grpc_completion_queue_create_for_next(nullptr);
  grpc_server_register_completion_queue(args.server, args.cq, nullptr);
  grpc_server_start(args.server);
  gpr_thd_new(&server, server_thread, &args, &options);

  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_new(&threads[i], create_loop_destroy, args.addr, &options);
  }
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_join(threads[i]);
  }
  grpc_server_shutdown_and_notify(args.server, args.cq, tag(0xd1e));

  gpr_thd_join(server);
  grpc_server_destroy(args.server);
  grpc_completion_queue_destroy(args.cq);
  gpr_free(args.addr);

  /* Third round, bogus tcp server */
  gpr_log(GPR_DEBUG, "Wave 3");
  args.pollset = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(args.pollset, &args.mu);
  gpr_event_init(&args.ready);
  gpr_thd_new(&server, bad_server_thread, &args, &options);
  gpr_event_wait(&args.ready, gpr_inf_future(GPR_CLOCK_MONOTONIC));

  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_new(&threads[i], create_loop_destroy, args.addr, &options);
  }
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_join(threads[i]);
  }

  gpr_atm_rel_store(&args.stop, 1);
  gpr_thd_join(server);
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_pollset_shutdown(&exec_ctx, args.pollset,
                        GRPC_CLOSURE_CREATE(done_pollset_shutdown, args.pollset,
                                            grpc_schedule_on_exec_ctx));
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_shutdown();
  return 0;
}

void watches_with_short_timeouts(void* addr) {
  for (int i = 0; i < NUM_OUTER_LOOPS_SHORT_TIMEOUTS; ++i) {
    grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
    grpc_channel* chan =
        grpc_insecure_channel_create((char*)addr, nullptr, nullptr);

    for (int j = 0; j < NUM_INNER_LOOPS_SHORT_TIMEOUTS; ++j) {
      gpr_timespec later_time =
          grpc_timeout_milliseconds_to_deadline(DELAY_MILLIS_SHORT_TIMEOUTS);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(chan, 0);
      GPR_ASSERT(state == GRPC_CHANNEL_IDLE);
      grpc_channel_watch_connectivity_state(chan, state, later_time, cq,
                                            nullptr);
      gpr_timespec poll_time =
          grpc_timeout_milliseconds_to_deadline(POLL_MILLIS_SHORT_TIMEOUTS);
      grpc_event ev = grpc_completion_queue_next(cq, poll_time, nullptr);
      GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
      GPR_ASSERT(ev.success == false);
      /* check that the watcher from "watch state" was free'd */
      GPR_ASSERT(grpc_channel_num_external_connectivity_watchers(chan) == 0);
    }
    grpc_channel_destroy(chan);
    grpc_completion_queue_destroy(cq);
  }
}

// This test tries to catch deadlock situations.
// With short timeouts on "watches" and long timeouts on cq next calls,
// so that a QUEUE_TIMEOUT likely means that something is stuck.
int run_concurrent_watches_with_short_timeouts_test() {
  grpc_init();

  gpr_thd_id threads[NUM_THREADS];

  char* localhost = gpr_strdup("localhost:54321");
  gpr_thd_options options = gpr_thd_options_default();
  gpr_thd_options_set_joinable(&options);

  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_new(&threads[i], watches_with_short_timeouts, localhost, &options);
  }
  for (size_t i = 0; i < NUM_THREADS; ++i) {
    gpr_thd_join(threads[i]);
  }
  gpr_free(localhost);

  grpc_shutdown();
  return 0;
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);

  run_concurrent_connectivity_test();
  run_concurrent_watches_with_short_timeouts_test();
}
