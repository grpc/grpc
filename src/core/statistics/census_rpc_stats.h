/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPC_INTERNAL_STATISTICS_CENSUS_RPC_STATS_H__
#define __GRPC_INTERNAL_STATISTICS_CENSUS_RPC_STATS_H__

#include "src/core/statistics/census_interface.h"
#include <grpc/support/port_platform.h>

struct census_rpc_stats {
  gpr_uint64 cnt;
  gpr_uint64 rpc_error_cnt;
  gpr_uint64 app_error_cnt;
  double elapsed_time_ms;
  double api_request_bytes;
  double wire_request_bytes;
  double api_response_bytes;
  double wire_response_bytes;
};

/* Creates an empty rpc stats object on heap. */
census_rpc_stats* census_rpc_stats_create_empty();

typedef struct census_per_service_per_method_rpc_stats {
  const char* service;
  const char* method;
  census_rpc_stats data;
} census_per_service_per_method_rpc_stats;

typedef struct census_aggregated_rpc_stats {
  int num_entries;
  census_per_service_per_method_rpc_stats* stats;
} census_aggregated_rpc_stats;

/* Deletes aggregated data. */
void census_aggregated_rpc_stats_destroy(census_aggregated_rpc_stats* data);

/* Records client side stats of a rpc. */
void census_record_rpc_client_stats(census_op_id op_id,
                                    const census_rpc_stats* stats);

/* Records server side stats of a rpc. */
void census_record_rpc_server_stats(census_op_id op_id,
                                    const census_rpc_stats* stats);

/* The following two functions are intended for inprocess query of
   per-service per-method stats from grpc implementations. */

/* Populates *data_map with server side aggregated per-service per-method
   stats.
   DO NOT CALL from outside of grpc code. */
void census_get_server_stats(census_aggregated_rpc_stats* data_map);

/* Populates *data_map with client side aggregated per-service per-method
   stats.
   DO NOT CALL from outside of grpc code. */
void census_get_client_stats(census_aggregated_rpc_stats* data_map);

#endif  /* __GRPC_INTERNAL_STATISTICS_CENSUS_RPC_STATS_H__ */
