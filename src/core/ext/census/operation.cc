/*
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

#include <grpc/census.h>

/* TODO(aveitch): These are all placeholder implementations. */

census_timestamp census_start_rpc_op_timestamp(void) {
  census_timestamp ct;
  /* TODO(aveitch): assumes gpr_timespec implementation of census_timestamp. */
  ct.ts = gpr_now(GPR_CLOCK_MONOTONIC);
  return ct;
}

census_context *census_start_client_rpc_op(
    const census_context *context, int64_t rpc_name_id,
    const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask,
    const census_timestamp *start_time) {
  return NULL;
}

census_context *census_start_server_rpc_op(
    const char *buffer, int64_t rpc_name_id,
    const census_rpc_name_info *rpc_name_info, const char *peer, int trace_mask,
    census_timestamp *start_time) {
  return NULL;
}

census_context *census_start_op(census_context *context, const char *family,
                                const char *name, int trace_mask) {
  return NULL;
}

void census_end_op(census_context *context, int status) {}
