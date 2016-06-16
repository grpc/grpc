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

#include <stdio.h>
#include "grpc/grpc_c_public.h"
#include "grpc/status_public.h"
#include "grpc/channel_public.h"
#include "grpc/message_public.h"

int main(int argc, char **argv) {
  // Local greetings server
  grpc_channel *chan = GRPC_channel_create("0.0.0.0:50051");

  grpc_method method = { NORMAL_RPC, "/helloworld.Greeter/SayHello" };
  grpc_context *context = grpc_context_create(chan);
  // hardcoded string for "gRPC-C"
  char str[] = { 0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43 };
  grpc_message msg = { str, sizeof(str) };
  // using char array to hold RPC result while protobuf is not there yet
  grpc_message resp;
  grpc_unary_blocking_call(chan, &method, context, msg, &resp);
  printf("Server said: %s\n", ((char *) resp.data) + 2);    // skip to the string in serialized protobuf object
  GRPC_message_destroy(&resp);

  GRPC_context_destroy(&context);
  GRPC_channel_destroy(&chan);
  return 0;
}
