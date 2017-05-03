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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H

#include <stdbool.h>

#include <grpc/impl/codegen/grpc_types.h>

typedef struct grpc_grpclb_client_stats grpc_grpclb_client_stats;

grpc_grpclb_client_stats* grpc_grpclb_client_stats_create();
grpc_grpclb_client_stats* grpc_grpclb_client_stats_ref(
    grpc_grpclb_client_stats* client_stats);
void grpc_grpclb_client_stats_unref(grpc_grpclb_client_stats* client_stats);

void grpc_grpclb_client_stats_add_call_started(
    grpc_grpclb_client_stats* client_stats);
void grpc_grpclb_client_stats_add_call_finished(
    bool finished_with_drop_for_rate_limiting,
    bool finished_with_drop_for_load_balancing,
    bool finished_with_client_failed_to_send, bool finished_known_received,
    grpc_grpclb_client_stats* client_stats);

void grpc_grpclb_client_stats_get(
    grpc_grpclb_client_stats* client_stats, int64_t* num_calls_started,
    int64_t* num_calls_finished,
    int64_t* num_calls_finished_with_drop_for_rate_limiting,
    int64_t* num_calls_finished_with_drop_for_load_balancing,
    int64_t* num_calls_finished_with_client_failed_to_send,
    int64_t* num_calls_finished_known_received);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_CLIENT_STATS_H \
          */
