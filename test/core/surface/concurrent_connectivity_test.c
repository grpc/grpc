/*
 *
 * Copyright 2016, Google Inc.
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

static void *tag(int n) { return (void *)(uintptr_t)n; }
static int detag(void *p) { return (int)(uintptr_t)p; }

void create_loop_destroy(void *addr) {
  for (int i = 0; i < NUM_OUTER_LOOPS; ++i) {
    grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
    grpc_channel *chan = grpc_insecure_channel_create((char *)addr, NULL, NULL);

    for (int j = 0; j < NUM_INNER_LOOPS; ++j) {
      gpr_timespec later_time =
          grpc_timeout_milliseconds_to_deadline(DELAY_MILLIS);
      grpc_connectivity_state state =
          grpc_channel_check_connectivity_state(chan, 1);
      grpc_channel_watch_connectivity_state(chan, state, later_time, cq, NULL);
      gpr_timespec poll_time =
          grpc_timeout_milliseconds_to_deadline(POLL_MILLIS);
      GPR_ASSERT(grpc_completion_queue_next(cq, poll_time, NULL).type ==
                 GRPC_OP_COMPLETE);
    }
    grpc_channel_destroy(chan);
    grpc_completion_queue_destroy(cq);
  }
}

struct server_thread_args {
  char *addr;
  grpc_server *server;
  grpc_completion_queue *cq;
  grpc_pollset *pollset;
  gpr_mu *mu;
  gpr_event ready;
  gpr_atm stop;
};

void server_thread(void *vargs) {
  struct server_thread_args *args = (struct server_thread_args *)vargs;
  grpc_event ev;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  ev = grpc_completion_queue_next(args->cq, deadline, NULL);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(detag(ev.tag) == 0xd1e);
}

static void on_connect(grpc_exec_ctx *exec_ctx, void *vargs, grpc_endpoint *tcp,
                       grpc_pollset *accepting_pollset,
                       grpc_tcp_server_acceptor *acceptor) {
  gpr_free(acceptor);
  struct server_thread_args *args = (struct server_thread_args *)vargs;
  grpc_endpoint_shutdown(exec_ctx, tcp,
                         GRPC_ERROR_CREATE_FROM_STATIC_STRING("Connected"));
  grpc_endpoint_destroy(exec_ctx, tcp);
  GRPC_LOG_IF_ERROR("pollset_kick", grpc_pollset_kick(args->pollset, NULL));
}

void bad_server_thread(void *vargs) {
  struct server_thread_args *args = (struct server_thread_args *)vargs;

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_resolved_address resolved_addr;
  struct sockaddr_storage *addr = (struct sockaddr_storage *)resolved_addr.addr;
  int port;
  grpc_tcp_server *s;
  grpc_error *error = grpc_tcp_server_create(&exec_ctx, NULL, NULL, &s);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  memset(&resolved_addr, 0, sizeof(resolved_addr));
  addr->ss_family = AF_INET;
  error = grpc_tcp_server_add_port(s, &resolved_addr, &port);
  GPR_ASSERT(GRPC_LOG_IF_ERROR("grpc_tcp_server_add_port", error));
  GPR_ASSERT(port > 0);
  gpr_asprintf(&args->addr, "localhost:%d", port);

  grpc_tcp_server_start(&exec_ctx, s, &args->pollset, 1, on_connect, args);
  gpr_event_set(&args->ready, (void *)1);

  gpr_mu_lock(args->mu);
  while (gpr_atm_acq_load(&args->stop) == 0) {
    gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
    gpr_timespec deadline =
        gpr_time_add(now, gpr_time_from_millis(100, GPR_TIMESPAN));

    grpc_pollset_worker *worker = NULL;
    if (!GRPC_LOG_IF_ERROR("pollset_work",
                           grpc_pollset_work(&exec_ctx, args->pollset, &worker,
                                             now, deadline))) {
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

static void done_pollset_shutdown(grpc_exec_ctx *exec_ctx, void *pollset,
                                  grpc_error *error) {
  grpc_pollset_destroy(pollset);
  gpr_free(pollset);
}

int main(int argc, char **argv) {
  struct server_thread_args args;
  memset(&args, 0, sizeof(args));

  grpc_test_init(argc, argv);
  grpc_init();

  gpr_thd_id threads[NUM_THREADS];
  gpr_thd_id server;

  char *localhost = gpr_strdup("localhost:54321");
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
  args.server = grpc_server_create(NULL, NULL);
  grpc_server_add_insecure_http2_port(args.server, args.addr);
  args.cq = grpc_completion_queue_create(NULL);
  grpc_server_register_completion_queue(args.server, args.cq, NULL);
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
  args.pollset = gpr_zalloc(grpc_pollset_size());
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
                        grpc_closure_create(done_pollset_shutdown, args.pollset,
                                            grpc_schedule_on_exec_ctx));
  grpc_exec_ctx_finish(&exec_ctx);

  grpc_shutdown();
  return 0;
}
