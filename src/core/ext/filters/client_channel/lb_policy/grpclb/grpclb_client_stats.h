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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H

#include <stdbool.h>

#include <grpc/impl/codegen/grpc_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct grpc_grpclb_client_stats grpc_grpclb_client_stats;

typedef struct {
  char* token;
  int64_t count;
} grpc_grpclb_drop_token_count;

typedef struct {
  grpc_grpclb_drop_token_count* token_counts;
  size_t num_entries;
} grpc_grpclb_dropped_call_counts;

grpc_grpclb_client_stats* grpc_grpclb_client_stats_create();
grpc_grpclb_client_stats* grpc_grpclb_client_stats_ref(
    grpc_grpclb_client_stats* client_stats);
void grpc_grpclb_client_stats_unref(grpc_grpclb_client_stats* client_stats);

void grpc_grpclb_client_stats_add_call_started(
    grpc_grpclb_client_stats* client_stats);
void grpc_grpclb_client_stats_add_call_finished(
    bool finished_with_client_failed_to_send, bool finished_known_received,
    grpc_grpclb_client_stats* client_stats);

// This method is not thread-safe; caller must synchronize.
void grpc_grpclb_client_stats_add_call_dropped_locked(
    char* token, grpc_grpclb_client_stats* client_stats);

// This method is not thread-safe; caller must synchronize.
void grpc_grpclb_client_stats_get_locked(
    grpc_grpclb_client_stats* client_stats, int64_t* num_calls_started,
    int64_t* num_calls_finished,
    int64_t* num_calls_finished_with_client_failed_to_send,
    int64_t* num_calls_finished_known_received,
    grpc_grpclb_dropped_call_counts** drop_token_counts);

void grpc_grpclb_dropped_call_counts_destroy(
    grpc_grpclb_dropped_call_counts* drop_entries);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H \
        */
