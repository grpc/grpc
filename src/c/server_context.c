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

#include "src/c/server_context.h"
#include <grpc/support/alloc.h>
#include "src/c/alloc.h"

GRPC_server_context *GRPC_server_context_create(GRPC_server *server) {
  GRPC_server_context *context = GRPC_ALLOC_STRUCT(
      GRPC_server_context,
      {.deadline = gpr_inf_future(GPR_CLOCK_REALTIME),
       .serialization_impl = {.serialize = NULL, .deserialize = NULL},
       .server = server});

  grpc_metadata_array_init(&context->send_trailing_metadata_array);
  return context;
}

// We define a conversion function instead of type-casting, which lets the user
// convert
// from any pointer to a grpc_context.
GRPC_context *GRPC_server_context_to_base(GRPC_server_context *server_context) {
  return (GRPC_context *)server_context;
}

void GRPC_server_context_destroy(GRPC_server_context **context) {
  GRPC_context_destroy(GRPC_server_context_to_base(*context));
  gpr_free(*context);
  *context = NULL;
}
