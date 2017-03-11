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

#include "test/core/end2end/end2end_tests.h"

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#include "test/core/end2end/cq_verifier.h"

static void *tag(intptr_t t) { return (void *)t; }

typedef struct {
  gpr_event started;
  grpc_channel *channel;
  grpc_completion_queue *cq;
} child_events;

static void child_thread(void *arg) {
  child_events *ce = arg;
  grpc_event ev;
  gpr_event_set(&ce->started, (void *)1);
  gpr_log(GPR_DEBUG, "verifying");
  ev = grpc_completion_queue_next(ce->cq, gpr_inf_future(GPR_CLOCK_MONOTONIC),
                                  NULL);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == tag(1));
  GPR_ASSERT(ev.success == 0);
}

static void test_connectivity(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(NULL, NULL);
  grpc_connectivity_state state;
  cq_verifier *cqv = cq_verifier_create(f.cq);
  child_events ce;
  gpr_thd_options thdopt = gpr_thd_options_default();
  gpr_thd_id thdid;

  grpc_channel_args client_args;
  grpc_arg arg_array[1];
  arg_array[0].type = GRPC_ARG_INTEGER;
  arg_array[0].key = "grpc.testing.fixed_reconnect_backoff_ms";
  arg_array[0].value.integer = 1000;
  client_args.args = arg_array;
  client_args.num_args = 1;

  config.init_client(&f, &client_args);

  ce.channel = f.client;
  ce.cq = f.cq;
  gpr_event_init(&ce.started);
  gpr_thd_options_set_joinable(&thdopt);
  GPR_ASSERT(gpr_thd_new(&thdid, child_thread, &ce, &thdopt));

  gpr_event_wait(&ce.started, gpr_inf_future(GPR_CLOCK_MONOTONIC));

  /* channels should start life in IDLE, and stay there */
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 0) ==
             GRPC_CHANNEL_IDLE);
  gpr_sleep_until(grpc_timeout_milliseconds_to_deadline(100));
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 0) ==
             GRPC_CHANNEL_IDLE);

  /* start watching for a change */
  gpr_log(GPR_DEBUG, "watching");
  grpc_channel_watch_connectivity_state(
      f.client, GRPC_CHANNEL_IDLE, gpr_now(GPR_CLOCK_MONOTONIC), f.cq, tag(1));

  /* eventually the child thread completion should trigger */
  gpr_thd_join(thdid);

  /* check that we're still in idle, and start connecting */
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 1) ==
             GRPC_CHANNEL_IDLE);
  /* start watching for a change */
  grpc_channel_watch_connectivity_state(f.client, GRPC_CHANNEL_IDLE,
                                        grpc_timeout_seconds_to_deadline(3),
                                        f.cq, tag(2));

  /* and now the watch should trigger */
  CQ_EXPECT_COMPLETION(cqv, tag(2), 1);
  cq_verify(cqv);
  state = grpc_channel_check_connectivity_state(f.client, 0);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
             state == GRPC_CHANNEL_CONNECTING);

  /* quickly followed by a transition to TRANSIENT_FAILURE */
  grpc_channel_watch_connectivity_state(f.client, GRPC_CHANNEL_CONNECTING,
                                        grpc_timeout_seconds_to_deadline(3),
                                        f.cq, tag(3));
  CQ_EXPECT_COMPLETION(cqv, tag(3), 1);
  cq_verify(cqv);
  state = grpc_channel_check_connectivity_state(f.client, 0);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
             state == GRPC_CHANNEL_CONNECTING);

  gpr_log(GPR_DEBUG, "*** STARTING SERVER ***");

  /* now let's bring up a server to connect to */
  config.init_server(&f, NULL);

  gpr_log(GPR_DEBUG, "*** STARTED SERVER ***");

  /* we'll go through some set of transitions (some might be missed), until
     READY is reached */
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        f.client, state, grpc_timeout_seconds_to_deadline(3), f.cq, tag(4));
    CQ_EXPECT_COMPLETION(cqv, tag(4), 1);
    cq_verify(cqv);
    state = grpc_channel_check_connectivity_state(f.client, 0);
    GPR_ASSERT(state == GRPC_CHANNEL_READY ||
               state == GRPC_CHANNEL_CONNECTING ||
               state == GRPC_CHANNEL_TRANSIENT_FAILURE);
  }

  /* bring down the server again */
  /* we should go immediately to TRANSIENT_FAILURE */
  gpr_log(GPR_DEBUG, "*** SHUTTING DOWN SERVER ***");

  grpc_channel_watch_connectivity_state(f.client, GRPC_CHANNEL_READY,
                                        grpc_timeout_seconds_to_deadline(3),
                                        f.cq, tag(5));

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));

  CQ_EXPECT_COMPLETION(cqv, tag(5), 1);
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead), 1);
  cq_verify(cqv);
  state = grpc_channel_check_connectivity_state(f.client, 0);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
             state == GRPC_CHANNEL_CONNECTING || state == GRPC_CHANNEL_IDLE);

  /* cleanup server */
  grpc_server_destroy(f.server);

  gpr_log(GPR_DEBUG, "*** SHUTDOWN SERVER ***");

  grpc_channel_destroy(f.client);
  grpc_completion_queue_shutdown(f.cq);
  grpc_completion_queue_destroy(f.cq);
  config.tear_down_data(&f);

  cq_verifier_destroy(cqv);
}

void connectivity(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION);
  test_connectivity(config);
}

void connectivity_pre_init(void) {}
