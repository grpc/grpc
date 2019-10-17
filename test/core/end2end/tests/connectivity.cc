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

#include "test/core/end2end/end2end_tests.h"

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/thd.h"
#include "test/core/end2end/cq_verifier.h"

static void* tag(intptr_t t) { return (void*)t; }

typedef struct {
  gpr_event started;
  grpc_channel* channel;
  grpc_completion_queue* cq;
} child_events;

struct CallbackContext {
  grpc_experimental_completion_queue_functor functor;
  gpr_event finished;
  explicit CallbackContext(void (*cb)(
      grpc_experimental_completion_queue_functor* functor, int success)) {
    functor.functor_run = cb;
    functor.inlineable = false;
    gpr_event_init(&finished);
  }
};

static void child_thread(void* arg) {
  child_events* ce = static_cast<child_events*>(arg);
  grpc_event ev;
  gpr_event_set(&ce->started, (void*)1);
  gpr_log(GPR_DEBUG, "verifying");
  ev = grpc_completion_queue_next(ce->cq, gpr_inf_future(GPR_CLOCK_MONOTONIC),
                                  nullptr);
  GPR_ASSERT(ev.type == GRPC_OP_COMPLETE);
  GPR_ASSERT(ev.tag == tag(1));
  GPR_ASSERT(ev.success == 0);
}

static void test_connectivity(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);
  grpc_connectivity_state state;
  cq_verifier* cqv = cq_verifier_create(f.cq);
  child_events ce;

  grpc_channel_args client_args;
  grpc_arg arg_array[1];
  arg_array[0].type = GRPC_ARG_INTEGER;
  arg_array[0].key =
      const_cast<char*>("grpc.testing.fixed_reconnect_backoff_ms");
  arg_array[0].value.integer = 1000;
  client_args.args = arg_array;
  client_args.num_args = 1;

  config.init_client(&f, &client_args);

  ce.channel = f.client;
  ce.cq = f.cq;
  gpr_event_init(&ce.started);
  grpc_core::Thread thd("grpc_connectivity", child_thread, &ce);
  thd.Start();

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
  thd.Join();

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
  config.init_server(&f, nullptr);

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

  /* shutdown_cq is not used in this test */
  grpc_completion_queue_destroy(f.shutdown_cq);
  config.tear_down_data(&f);

  cq_verifier_destroy(cqv);
}

static void cb_watch_connectivity(
    grpc_experimental_completion_queue_functor* functor, int success) {
  CallbackContext* cb_ctx = (CallbackContext*)functor;

  gpr_log(GPR_DEBUG, "cb_watch_connectivity called, verifying");

  /* callback must not have errors */
  GPR_ASSERT(success != 0);

  gpr_event_set(&cb_ctx->finished, (void*)1);
}

static void cb_shutdown(grpc_experimental_completion_queue_functor* functor,
                        int /*success*/) {
  CallbackContext* cb_ctx = (CallbackContext*)functor;

  gpr_log(GPR_DEBUG, "cb_shutdown called, nothing to do");
  gpr_event_set(&cb_ctx->finished, (void*)1);
}

static void test_watch_connectivity_cq_callback(
    grpc_end2end_test_config config) {
  CallbackContext cb_ctx(cb_watch_connectivity);
  CallbackContext cb_shutdown_ctx(cb_shutdown);
  grpc_completion_queue* cq;
  grpc_end2end_test_fixture f = config.create_fixture(nullptr, nullptr);

  config.init_client(&f, nullptr);

  /* start connecting */
  grpc_channel_check_connectivity_state(f.client, 1);

  /* create the cq callback */
  cq = grpc_completion_queue_create_for_callback(&cb_shutdown_ctx.functor,
                                                 nullptr);

  /* start watching for any change, cb is immediately called
   * and no dead lock should be raised */
  grpc_channel_watch_connectivity_state(f.client, GRPC_CHANNEL_IDLE,
                                        grpc_timeout_seconds_to_deadline(3), cq,
                                        &cb_ctx.functor);

  /* we just check that the callback was executed once notifying a connection
   * transition */
  GPR_ASSERT(gpr_event_wait(&cb_ctx.finished,
                            gpr_inf_future(GPR_CLOCK_MONOTONIC)) != nullptr);

  /* shutdown, since shutdown cb might be executed in a background thread
   * we actively wait till is executed. */
  grpc_completion_queue_shutdown(cq);
  gpr_event_wait(&cb_shutdown_ctx.finished,
                 gpr_inf_future(GPR_CLOCK_MONOTONIC));

  /* cleanup */
  grpc_channel_destroy(f.client);
  grpc_completion_queue_destroy(cq);

  /* shutdown_cq and cq are not used in this test */
  grpc_completion_queue_destroy(f.cq);
  grpc_completion_queue_destroy(f.shutdown_cq);

  config.tear_down_data(&f);
}

void connectivity(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION);
  test_connectivity(config);
  test_watch_connectivity_cq_callback(config);
}

void connectivity_pre_init(void) {}
