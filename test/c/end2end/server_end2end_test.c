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
 * This tests both the C client and the server.
 */

#include "test/c/end2end/server_end2end_test.h"
#include "test/core/util/port.h"
#include "src/proto/grpc/testing/echo.grpc.pbc.h"
#include <third_party/nanopb/pb_encode.h>
#include <third_party/nanopb/pb_decode.h>
#include <grpc/support/log.h>
#include <stdio.h>
#include <pthread.h>

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

static void *client_thread(void *param) {
  char *host = param;
  GRPC_channel *channel = GRPC_channel_create(host);
  grpc_testing_EchoRequest request = {.message = {.arg = "gRPC-C", .funcs.encode = write_string_from_arg}};
  grpc_testing_EchoResponse response = {.message = {.funcs.decode = read_string_store_in_arg}};

  GRPC_client_context *context = GRPC_client_context_create(channel);
  GRPC_status status = grpc_testing_EchoTestService_Echo(context, request, &response);
  GPR_ASSERT(status.ok);
  GPR_ASSERT(status.code == GRPC_STATUS_OK);
  GPR_ASSERT(response.message.arg != NULL);
  GPR_ASSERT(strcmp(response.message.arg, "gRPC-C") == 0);
  free(response.message.arg);
  GRPC_client_context_destroy(&context);
  GRPC_channel_destroy(&channel);

  return NULL;
}

typedef struct async_server_data {
  GRPC_server_context *context;
  grpc_testing_EchoRequest request;
  grpc_testing_EchoResponse reply;
} async_server_data;

int main(int argc, char **argv) {
  GRPC_server *server = GRPC_build_server((GRPC_build_server_options){ .max_message_size = 0 });
  GRPC_incoming_notification_queue *incoming =
    GRPC_server_new_incoming_queue(server);
  int port = grpc_pick_unused_port_or_die();
  char *host = malloc(100);
  sprintf(host, "0.0.0.0:%d", port);
  GRPC_server_listen_host(server, host);
  GRPC_registered_service *service = grpc_testing_EchoTestService_Register(server);
  GRPC_server_start(server);

  // Start client
  pthread_t tid;
  pthread_create(&tid, NULL, client_thread, host);

  // Run server
  {
    async_server_data *data = calloc(sizeof(async_server_data), 1);
    data->context = GRPC_server_context_create(server);
    data->request.message.funcs.decode = read_string_store_in_arg;
    data->reply.message.funcs.encode = write_string_from_arg;

    data->request.message.arg = NULL;
    data->reply.message.arg = NULL;

    // Listen for this method
    GRPC_server_async_response_writer *writer =
      grpc_testing_EchoTestService_Echo_ServerRequest(
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

    GPR_ASSERT(queue_status == GRPC_COMPLETION_QUEUE_GOT_EVENT);
    if (!ok) {
      async_server_data *data_new = (async_server_data *) tag;
      char *bad_reply = calloc(4, 1);
      data_new->reply.message.arg = bad_reply;

      grpc_testing_EchoTestService_Echo_ServerFinish(writer, &data_new->reply,
                                               GRPC_STATUS_DATA_LOSS, data_new);
    } else {
      // Process the request
      async_server_data *data_new = (async_server_data *) tag;
      char *input_str = data_new->request.message.arg;
      size_t output_len = strlen(input_str);
      char *output_str = malloc(output_len + 1);
      sprintf(output_str, "%s", input_str);
      data_new->reply.message.arg = output_str;

      grpc_testing_EchoTestService_Echo_ServerFinish(writer, &data_new->reply,
                                               GRPC_STATUS_OK, data_new);
    }

    // Wait for request termination
    queue_status = GRPC_completion_queue_next(incoming->cq, &tag, &ok);

    GPR_ASSERT(queue_status == GRPC_COMPLETION_QUEUE_GOT_EVENT);
    GPR_ASSERT(ok);

    // Clean up
    {
      async_server_data *data_new = (async_server_data *)tag;
      free(data_new->request.message.arg);
      free(data_new->reply.message.arg);
      GRPC_server_context_destroy(&data_new->context);
      free(data_new);
    }
  }
  GRPC_server_shutdown(server);
  GRPC_server_destroy(server);

  // Wait for client to return
  pthread_join(tid, NULL);

  return 0;
}

