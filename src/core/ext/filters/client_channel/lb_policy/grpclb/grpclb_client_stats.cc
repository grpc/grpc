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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/atm.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/channel/channel_args.h"

#define GRPC_ARG_GRPCLB_CLIENT_STATS "grpc.grpclb_client_stats"

struct grpc_grpclb_client_stats {
  gpr_refcount refs;
  // This field must only be accessed via *_locked() methods.
  grpc_grpclb_dropped_call_counts* drop_token_counts;
  // These fields may be accessed from multiple threads at a time.
  gpr_atm num_calls_started;
  gpr_atm num_calls_finished;
  gpr_atm num_calls_finished_with_client_failed_to_send;
  gpr_atm num_calls_finished_known_received;
};

grpc_grpclb_client_stats* grpc_grpclb_client_stats_create() {
  grpc_grpclb_client_stats* client_stats =
      static_cast<grpc_grpclb_client_stats*>(gpr_zalloc(sizeof(*client_stats)));
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
    grpc_grpclb_dropped_call_counts_destroy(client_stats->drop_token_counts);
    gpr_free(client_stats);
  }
}

void grpc_grpclb_client_stats_add_call_started(
    grpc_grpclb_client_stats* client_stats) {
  gpr_atm_full_fetch_add(&client_stats->num_calls_started, (gpr_atm)1);
}

void grpc_grpclb_client_stats_add_call_finished(
    bool finished_with_client_failed_to_send, bool finished_known_received,
    grpc_grpclb_client_stats* client_stats) {
  gpr_atm_full_fetch_add(&client_stats->num_calls_finished, (gpr_atm)1);
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

void grpc_grpclb_client_stats_add_call_dropped_locked(
    char* token, grpc_grpclb_client_stats* client_stats) {
  // Increment num_calls_started and num_calls_finished.
  gpr_atm_full_fetch_add(&client_stats->num_calls_started, (gpr_atm)1);
  gpr_atm_full_fetch_add(&client_stats->num_calls_finished, (gpr_atm)1);
  // Record the drop.
  if (client_stats->drop_token_counts == nullptr) {
    client_stats->drop_token_counts =
        static_cast<grpc_grpclb_dropped_call_counts*>(
            gpr_zalloc(sizeof(grpc_grpclb_dropped_call_counts)));
  }
  grpc_grpclb_dropped_call_counts* drop_token_counts =
      client_stats->drop_token_counts;
  for (size_t i = 0; i < drop_token_counts->num_entries; ++i) {
    if (strcmp(drop_token_counts->token_counts[i].token, token) == 0) {
      ++drop_token_counts->token_counts[i].count;
      return;
    }
  }
  // Not found, so add a new entry.  We double the size of the array each time.
  size_t new_num_entries = 2;
  while (new_num_entries < drop_token_counts->num_entries + 1) {
    new_num_entries *= 2;
  }
  drop_token_counts->token_counts = static_cast<grpc_grpclb_drop_token_count*>(
      gpr_realloc(drop_token_counts->token_counts,
                  new_num_entries * sizeof(grpc_grpclb_drop_token_count)));
  grpc_grpclb_drop_token_count* new_entry =
      &drop_token_counts->token_counts[drop_token_counts->num_entries++];
  new_entry->token = gpr_strdup(token);
  new_entry->count = 1;
}

static void atomic_get_and_reset_counter(int64_t* value, gpr_atm* counter) {
  *value = static_cast<int64_t>(gpr_atm_acq_load(counter));
  gpr_atm_full_fetch_add(counter, (gpr_atm)(-*value));
}

void grpc_grpclb_client_stats_get_locked(
    grpc_grpclb_client_stats* client_stats, int64_t* num_calls_started,
    int64_t* num_calls_finished,
    int64_t* num_calls_finished_with_client_failed_to_send,
    int64_t* num_calls_finished_known_received,
    grpc_grpclb_dropped_call_counts** drop_token_counts) {
  atomic_get_and_reset_counter(num_calls_started,
                               &client_stats->num_calls_started);
  atomic_get_and_reset_counter(num_calls_finished,
                               &client_stats->num_calls_finished);
  atomic_get_and_reset_counter(
      num_calls_finished_with_client_failed_to_send,
      &client_stats->num_calls_finished_with_client_failed_to_send);
  atomic_get_and_reset_counter(
      num_calls_finished_known_received,
      &client_stats->num_calls_finished_known_received);
  *drop_token_counts = client_stats->drop_token_counts;
  client_stats->drop_token_counts = nullptr;
}

void grpc_grpclb_dropped_call_counts_destroy(
    grpc_grpclb_dropped_call_counts* drop_entries) {
  if (drop_entries != nullptr) {
    for (size_t i = 0; i < drop_entries->num_entries; ++i) {
      gpr_free(drop_entries->token_counts[i].token);
    }
    gpr_free(drop_entries->token_counts);
    gpr_free(drop_entries);
  }
}
