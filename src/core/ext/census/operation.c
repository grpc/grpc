/*
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
