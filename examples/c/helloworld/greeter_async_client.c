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

/**
 * This file demonstrates the basic usage of async unary API.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "helloworld.grpc.pbc.h"

/**
 * Nanopb callbacks for string encoding/decoding.
 */

static bool write_string(pb_ostream_t *stream, const pb_field_t *field,
                         void *const *arg) {
  char *str = "world";
  if (!pb_encode_tag_for_field(stream, field)) return false;

  return pb_encode_string(stream, (uint8_t *)str, strlen(str));
}

static bool read_string(pb_istream_t *stream, const pb_field_t *field,
                        void **arg) {
  size_t len = stream->bytes_left;
  char *str = malloc(len + 1);
  if (!pb_read(stream, (uint8_t *) str, len)) return false;
  str[len] = '\0';
  printf("Server replied %s\n", str);
  free(str);
  return true;
}

int main(int argc, char **argv) {
  GRPC_channel *chan = GRPC_channel_create("0.0.0.0:50051");
  GRPC_client_context *context = GRPC_client_context_create(chan);
  GRPC_completion_queue *cq = GRPC_completion_queue_create();
  helloworld_HelloRequest request = {.name.funcs.encode = write_string};
  helloworld_HelloReply reply = {.message.funcs.decode = read_string};
  // this method returns immediately
  GRPC_client_async_response_reader *reader =
      helloworld_Greeter_SayHello_Async(context, cq, request);

  // set up finish notification via tag
  bool ok;
  void *tag;
  helloworld_Greeter_SayHello_Finish(reader, &reply, /*TAG*/ (void *)12345);

  // wait for async RPC to finish
  GRPC_completion_queue_operation_status queue_status =
      GRPC_completion_queue_next(cq, &tag, &ok);
  assert(queue_status == GRPC_COMPLETION_QUEUE_GOT_EVENT);
  assert(ok);
  assert(tag == (void *)12345);

  // get status from context
  GRPC_status status = GRPC_get_call_status(context);
  assert(status.ok);
  assert(status.code == GRPC_STATUS_OK);

  GRPC_completion_queue_shutdown(cq);
  while (GRPC_completion_queue_next(cq, tag, &ok) !=
         GRPC_COMPLETION_QUEUE_SHUTDOWN) {
  }
  GRPC_completion_queue_destroy(cq);

  GRPC_client_context_destroy(&context);
  GRPC_channel_destroy(&chan);
  return 0;
}
