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
#include <grpc/grpc.h>
#include "src/core/surface/call.h"

static void grpc_census_context_destroy(void *context) {
  census_context_destroy((census_context *)context);
}

void grpc_census_call_set_context(grpc_call *call, census_context *context) {
  if (!census_available()) {
    return;
  }
  if (context == NULL) {
    if (grpc_call_is_client(call)) {
      census_context *context_ptr;
      census_context_deserialize(NULL, &context_ptr);
      grpc_call_context_set(call, GRPC_CONTEXT_TRACING, context_ptr,
                            grpc_census_context_destroy);
    } else {
      /* TODO(aveitch): server side context code to be implemented. */
    }
  } else {
    grpc_call_context_set(call, GRPC_CONTEXT_TRACING, context, NULL);
  }
}

census_context *grpc_census_call_get_context(grpc_call *call) {
  return (census_context *)grpc_call_context_get(call, GRPC_CONTEXT_TRACING);
}
