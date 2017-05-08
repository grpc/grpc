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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/sync.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_args.h"

#define GRPC_ARG_GRPCLB_CLIENT_STATS "grpc.grpclb_client_stats"

struct grpc_grpclb_client_stats {
  gpr_refcount refs;
  gpr_atm num_calls_started;
  gpr_atm num_calls_finished;
  gpr_atm num_calls_finished_with_drop_for_rate_limiting;
  gpr_atm num_calls_finished_with_drop_for_load_balancing;
  gpr_atm num_calls_finished_with_client_failed_to_send;
  gpr_atm num_calls_finished_known_received;
};

grpc_grpclb_client_stats* grpc_grpclb_client_stats_create() {
  grpc_grpclb_client_stats* client_stats = gpr_zalloc(sizeof(*client_stats));
  gpr_ref_init(&client_stats->refs, 1);
  return client_stats;
}

grpc_grpclb_client_stats* grpc_grpclb_client_stats_ref(
    grpc_grpclb_client_stats* client_stats) {
  gpr_ref(&client_stats->refs);
  return client_stats;
}

void grpc_grpclb_client_stats_unref(grpc_grpclb_client_stats* client_stats) {
  if (gpr_unref(&client_stats->refs)) {
    gpr_free(client_stats);
  }
}

void grpc_grpclb_client_stats_add_call_started(
    grpc_grpclb_client_stats* client_stats) {
  gpr_atm_full_fetch_add(&client_stats->num_calls_started, (gpr_atm)1);
}

void grpc_grpclb_client_stats_add_call_finished(
    bool finished_with_drop_for_rate_limiting,
    bool finished_with_drop_for_load_balancing,
    bool finished_with_client_failed_to_send, bool finished_known_received,
    grpc_grpclb_client_stats* client_stats) {
  gpr_atm_full_fetch_add(&client_stats->num_calls_finished, (gpr_atm)1);
  if (finished_with_drop_for_rate_limiting) {
    gpr_atm_full_fetch_add(
        &client_stats->num_calls_finished_with_drop_for_rate_limiting,
        (gpr_atm)1);
  }
  if (finished_with_drop_for_load_balancing) {
    gpr_atm_full_fetch_add(
        &client_stats->num_calls_finished_with_drop_for_load_balancing,
        (gpr_atm)1);
  }
  if (finished_with_client_failed_to_send) {
    gpr_atm_full_fetch_add(
        &client_stats->num_calls_finished_with_client_failed_to_send,
        (gpr_atm)1);
  }
  if (finished_known_received) {
    gpr_atm_full_fetch_add(&client_stats->num_calls_finished_known_received,
                           (gpr_atm)1);
  }
}

static void atomic_get_and_reset_counter(int64_t* value, gpr_atm* counter) {
  *value = (int64_t)gpr_atm_acq_load(counter);
  gpr_atm_full_fetch_add(counter, (gpr_atm)(-*value));
}

void grpc_grpclb_client_stats_get(
    grpc_grpclb_client_stats* client_stats, int64_t* num_calls_started,
    int64_t* num_calls_finished,
    int64_t* num_calls_finished_with_drop_for_rate_limiting,
    int64_t* num_calls_finished_with_drop_for_load_balancing,
    int64_t* num_calls_finished_with_client_failed_to_send,
    int64_t* num_calls_finished_known_received) {
  atomic_get_and_reset_counter(num_calls_started,
                               &client_stats->num_calls_started);
  atomic_get_and_reset_counter(num_calls_finished,
                               &client_stats->num_calls_finished);
  atomic_get_and_reset_counter(
      num_calls_finished_with_drop_for_rate_limiting,
      &client_stats->num_calls_finished_with_drop_for_rate_limiting);
  atomic_get_and_reset_counter(
      num_calls_finished_with_drop_for_load_balancing,
      &client_stats->num_calls_finished_with_drop_for_load_balancing);
  atomic_get_and_reset_counter(
      num_calls_finished_with_client_failed_to_send,
      &client_stats->num_calls_finished_with_client_failed_to_send);
  atomic_get_and_reset_counter(
      num_calls_finished_known_received,
      &client_stats->num_calls_finished_known_received);
}
