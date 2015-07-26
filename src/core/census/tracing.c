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

#include <grpc/census.h>

/* TODO(aveitch): These are all placeholder implementations. */

int census_trace_mask(const census_context *context) {
  return CENSUS_TRACE_MASK_NONE;
}

census_context *census_start_client_rpc_op(census_context *context,
                                           const char *service,
                                           const char *method, const char *peer,
                                           int trace_mask,
                                           gpr_timespec *start_time) {
  return NULL;
}

void census_start_server_rpc_op(census_context *context, const char *service,
                                const char *method, const char *peer,
                                int trace_mask, gpr_timespec *start_time) {}

census_context *census_start_op(census_context *context, const char *family,
                                const char *name, int trace_mask,
                                gpr_timespec *start_time) {
  return NULL;
}

void census_trace_end_op(census_context *context, int status) {}

void census_trace_print(census_context *context, const char *buffer, size_t n) {
}

void census_get_active_ops_as_proto(/* pointer to proto */) {}

void census_get_trace_as_proto(/* pointer to proto */) {}
