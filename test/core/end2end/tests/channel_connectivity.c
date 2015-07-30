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
#include <grpc/support/time.h>

#include "test/core/end2end/cq_verifier.h"

static void *tag(gpr_intptr t) { return (void *)t; }

static void test_connectivity(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(NULL, NULL);
  grpc_connectivity_state state;
  cq_verifier *cqv = cq_verifier_create(f.cq);

  config.init_client(&f, NULL);

  /* channels should start life in IDLE, and stay there */
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 0) == GRPC_CHANNEL_IDLE);
  gpr_sleep_until(GRPC_TIMEOUT_MILLIS_TO_DEADLINE(100));
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 0) == GRPC_CHANNEL_IDLE);

  /* start watching for a change */
  grpc_channel_watch_connectivity_state(
  	f.client, GRPC_CHANNEL_IDLE, &state, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3), f.cq, tag(1));
  /* nothing should happen */
  cq_verify_empty(cqv);

  /* check that we're still in idle, and start connecting */
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 1) == GRPC_CHANNEL_IDLE);

  /* and now the watch should trigger */
  cq_expect_completion(cqv, tag(1), 1);
  cq_verify(cqv);
  GPR_ASSERT(state == GRPC_CHANNEL_CONNECTING);

  /* quickly followed by a transition to TRANSIENT_FAILURE */
  grpc_channel_watch_connectivity_state(
  	f.client, GRPC_CHANNEL_CONNECTING, &state, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3), f.cq, tag(2));
  cq_expect_completion(cqv, tag(2), 1);
  cq_verify(cqv);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE);

  gpr_log(GPR_DEBUG, "*** STARTING SERVER ***");

  /* now let's bring up a server to connect to */
  config.init_server(&f, NULL);

  gpr_log(GPR_DEBUG, "*** STARTED SERVER ***");

  /* we'll go through some set of transitions (some might be missed), until
     READY is reached */
  while (state != GRPC_CHANNEL_READY) {
  	grpc_channel_watch_connectivity_state(
  		f.client, state, &state, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3), f.cq, tag(3));
  	cq_expect_completion(cqv, tag(3), 1);
    cq_verify(cqv);
  	GPR_ASSERT(state == GRPC_CHANNEL_READY || state == GRPC_CHANNEL_CONNECTING || state == GRPC_CHANNEL_TRANSIENT_FAILURE);
  }

  /* bring down the server again */
  /* we should go immediately to TRANSIENT_FAILURE */
  gpr_log(GPR_DEBUG, "*** SHUTTING DOWN SERVER ***");

  grpc_channel_watch_connectivity_state(
  	f.client, GRPC_CHANNEL_READY, &state, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(3), f.cq, tag(4));

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));

  cq_expect_completion(cqv, tag(4), 1);
  cq_expect_completion(cqv, tag(0xdead), 1);
  cq_verify(cqv);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE);

  /* cleanup server */
  grpc_server_destroy(f.server);

  gpr_log(GPR_DEBUG, "*** SHUTDOWN SERVER ***");

  grpc_channel_destroy(f.client);
  grpc_completion_queue_shutdown(f.cq);
  grpc_completion_queue_destroy(f.cq);
  config.tear_down_data(&f);

  cq_verifier_destroy(cqv);
}

void grpc_end2end_tests(grpc_end2end_test_config config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION);
  test_connectivity(config);
}
