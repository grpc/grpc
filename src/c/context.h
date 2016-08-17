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

#ifndef GRPC_C_INTERNAL_CONTEXT_H
#define GRPC_C_INTERNAL_CONTEXT_H

#include <grpc/grpc.h>
#include <grpc_c/codegen/context.h>
#include <grpc_c/codegen/method.h>
#include <grpc_c/codegen/serialization.h>
#include <grpc_c/grpc_c.h>
#include <grpc_c/status.h>
#include <stdbool.h>
#include "src/c/message.h"

/**
 * Both client and server context shares this common stub.
 */
#define GRPC_C_CONTEXT_BASE_MEMBERS               \
  grpc_metadata *send_metadata_array;             \
  grpc_metadata_array recv_metadata_array;        \
  gpr_timespec deadline;                          \
                                                  \
  /* serialization mechanism used in this call */ \
  grpc_serialization_impl serialization_impl;     \
                                                  \
  /* state tracking */                            \
  bool initial_metadata_received;                 \
  GRPC_method rpc_method;                         \
  grpc_channel *channel;                          \
  grpc_call *call;

struct GRPC_context {
  GRPC_C_CONTEXT_BASE_MEMBERS
};

void GRPC_context_destroy(GRPC_context *context);

#endif  // GRPC_C_INTERNAL_CONTEXT_H
