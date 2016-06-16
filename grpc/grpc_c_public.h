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


#ifndef TEST_GRPC_C_GRPC_C_PUBLIC_H
#define TEST_GRPC_C_GRPC_C_PUBLIC_H

#include <stdlib.h>

typedef struct grpc_channel grpc_channel;
typedef struct grpc_status grpc_status;
typedef struct grpc_context grpc_context;

typedef struct grpc_method {
  enum RpcType {
    NORMAL_RPC = 0,
    CLIENT_STREAMING,  // request streaming
    SERVER_STREAMING,  // response streaming
    BIDI_STREAMING
  } type;
  const char* const name;
} grpc_method;

typedef struct grpc_message {
  void * data;
  size_t length;
} grpc_message;

grpc_context *grpc_context_create(grpc_channel *chan);
void GRPC_context_destroy(grpc_context **context);

grpc_status grpc_unary_blocking_call(grpc_channel *channel, const grpc_method * const rpc_method, grpc_context * const context, const grpc_message message, grpc_message *response);

#endif //TEST_GRPC_C_GRPC_C_PUBLIC_H
