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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "helloworld.grpc.pbc.h"

/**
 * Nanopb callbacks for string encoding/decoding.
 */

static bool write_string_from_arg(pb_ostream_t *stream, const pb_field_t *field,
                                  void *const *arg) {
  const char *str = *arg;
  if (!pb_encode_tag_for_field(stream, field)) return false;

  return pb_encode_string(stream, (uint8_t *)str, strlen(str));
}

/**
 * This callback function reads a string from Nanopb stream and copies it into
 * the callback args.
 * Users need to free the string after use.
 */
static bool read_string_store_in_arg(pb_istream_t *stream,
                                     const pb_field_t *field, void **arg) {
  size_t len = stream->bytes_left;
  char *str = malloc(len + 1);
  if (!pb_read(stream, (uint8_t *) str, len)) return false;
  str[len] = '\0';
  *arg = str;
  return true;
}

typedef struct async_server_data {
  GRPC_server_context *context;
  helloworld_HelloRequest request;
  helloworld_HelloReply reply;
} async_server_data;

int main(int argc, char **argv) {
  GRPC_server *server = GRPC_build_server((GRPC_build_server_options){});
  GRPC_incoming_notification_queue *incoming =
      GRPC_server_new_incoming_queue(server);
  GRPC_server_listen_host(server, "0.0.0.0:50051");
  GRPC_registered_service *service = helloworld_Greeter_Register(server);
  GRPC_server_start(server);
  // Run server
  for (;;) {
    async_server_data *data = calloc(sizeof(async_server_data), 1);
    data->context = GRPC_server_context_create(server);
    data->request.name.funcs.decode = read_string_store_in_arg;
    data->reply.message.funcs.encode = write_string_from_arg;

    data->request.name.arg = NULL;
    data->reply.message.arg = NULL;

    // Listen for this method
    GRPC_server_async_response_writer *writer =
        helloworld_Greeter_SayHello_ServerRequest(
            service, data->context, &data->request,
            incoming,      // incoming queue
            incoming->cq,  // processing queue - we can reuse the
                           // same underlying completion queue, or
                           // specify a different one here
            data           // tag for the completion queues
            );

    // Wait for incoming call
    void *tag;
    bool ok;
    GRPC_completion_queue_operation_status queue_status =
        GRPC_completion_queue_next(incoming->cq, &tag, &ok);

    if (queue_status == GRPC_COMPLETION_QUEUE_SHUTDOWN) break;
    assert(queue_status == GRPC_COMPLETION_QUEUE_GOT_EVENT);
    if (!ok) {
      async_server_data *data_new = (async_server_data *)tag;
      char *bad_reply = calloc(1, 1);
      data_new->reply.message.arg = bad_reply;

      helloworld_Greeter_SayHello_ServerFinish(writer, &data_new->reply,
                                               GRPC_STATUS_DATA_LOSS, data_new);
    } else {
      // Process the request
      async_server_data *data_new = (async_server_data *)tag;
      char *input_str = data_new->request.name.arg;
      size_t output_len = strlen(input_str) + 6;
      char *output_str = malloc(output_len + 1);
      sprintf(output_str, "Hello %s", input_str);
      data_new->reply.message.arg = output_str;

      helloworld_Greeter_SayHello_ServerFinish(writer, &data_new->reply,
                                               GRPC_STATUS_OK, data_new);
    }

    // Wait for request termination
    queue_status = GRPC_completion_queue_next(incoming->cq, &tag, &ok);

    if (queue_status == GRPC_COMPLETION_QUEUE_SHUTDOWN) break;
    assert(queue_status == GRPC_COMPLETION_QUEUE_GOT_EVENT);
    if (!ok) continue;

    // Clean up
    {
      async_server_data *data_new = (async_server_data *)tag;
      free(data_new->request.name.arg);
      free(data_new->reply.message.arg);
      GRPC_server_context_destroy(&data_new->context);
      free(data_new);
    }
  }
  GRPC_server_shutdown(server);
  GRPC_server_destroy(server);
  return 0;
}
