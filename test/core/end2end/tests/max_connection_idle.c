/*
 *
 * Copyright 2017, Google Inc.
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

#include <limits.h>
#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "test/core/end2end/cq_verifier.h"

#define MAX_CONNECTION_IDLE_MS 500
#define MAX_CONNECTION_AGE_MS 9999

static void *tag(intptr_t t) { return (void *)t; }

static void test_max_connection_idle(grpc_end2end_test_config config) {
  grpc_end2end_test_fixture f = config.create_fixture(NULL, NULL);
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  cq_verifier *cqv = cq_verifier_create(f.cq);

  grpc_arg client_a[] = {{.type = GRPC_ARG_INTEGER,
                          .key = "grpc.testing.fixed_reconnect_backoff_ms",
                          .value.integer = 1000}};
  grpc_arg server_a[] = {{.type = GRPC_ARG_INTEGER,
                          .key = GRPC_ARG_MAX_CONNECTION_IDLE_MS,
                          .value.integer = MAX_CONNECTION_IDLE_MS},
                         {.type = GRPC_ARG_INTEGER,
                          .key = GRPC_ARG_MAX_CONNECTION_AGE_MS,
                          .value.integer = MAX_CONNECTION_AGE_MS}};
  grpc_channel_args client_args = {.num_args = GPR_ARRAY_SIZE(client_a),
                                   .args = client_a};
  grpc_channel_args server_args = {.num_args = GPR_ARRAY_SIZE(server_a),
                                   .args = server_a};

  config.init_client(&f, &client_args);
  config.init_server(&f, &server_args);

  /* check that we're still in idle, and start connecting */
  GPR_ASSERT(grpc_channel_check_connectivity_state(f.client, 1) ==
             GRPC_CHANNEL_IDLE);
  /* we'll go through some set of transitions (some might be missed), until
     READY is reached */
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        f.client, state, grpc_timeout_seconds_to_deadline(3), f.cq, tag(99));
    CQ_EXPECT_COMPLETION(cqv, tag(99), 1);
    cq_verify(cqv);
    state = grpc_channel_check_connectivity_state(f.client, 0);
    GPR_ASSERT(state == GRPC_CHANNEL_READY ||
               state == GRPC_CHANNEL_CONNECTING ||
               state == GRPC_CHANNEL_TRANSIENT_FAILURE);
  }

  /* wait for the channel to reach its maximum idle time */
  grpc_channel_watch_connectivity_state(
      f.client, GRPC_CHANNEL_READY,
      grpc_timeout_milliseconds_to_deadline(MAX_CONNECTION_IDLE_MS + 3000),
      f.cq, tag(99));
  CQ_EXPECT_COMPLETION(cqv, tag(99), 1);
  cq_verify(cqv);
  state = grpc_channel_check_connectivity_state(f.client, 0);
  GPR_ASSERT(state == GRPC_CHANNEL_TRANSIENT_FAILURE ||
             state == GRPC_CHANNEL_CONNECTING || state == GRPC_CHANNEL_IDLE);

  grpc_server_shutdown_and_notify(f.server, f.cq, tag(0xdead));
  CQ_EXPECT_COMPLETION(cqv, tag(0xdead), 1);
  cq_verify(cqv);

  grpc_server_destroy(f.server);
  grpc_channel_destroy(f.client);
  grpc_completion_queue_shutdown(f.cq);
  grpc_completion_queue_destroy(f.cq);
  config.tear_down_data(&f);

  cq_verifier_destroy(cqv);
}

void max_connection_idle(grpc_end2end_test_config config) {
  test_max_connection_idle(config);
}

void max_connection_idle_pre_init(void) {}
