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
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "grpc/status_code_public.h"
#include "grpc/grpc_c_public.h"
#include "grpc/status_public.h"
#include "grpc/context_public.h"
#include "grpc/channel_public.h"
#include "grpc/unary_async_call_public.h"
#include "grpc/unary_blocking_call_public.h"
#include "grpc/client_streaming_blocking_call_public.h"
#include "grpc/server_streaming_blocking_call_public.h"

static void async_say_hello(GRPC_channel *chan, GRPC_completion_queue *cq);
static void *async_say_hello_worker(void *param);

int main(int argc, char **argv) {
  // Local greetings server
  GRPC_channel *chan = GRPC_channel_create("0.0.0.0:50051");

  {
    printf("Testing sync unary call\n");
    GRPC_method method = {NORMAL_RPC, "/helloworld.Greeter/SayHello"};
    GRPC_context *context = GRPC_context_create(chan);
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    GRPC_message resp;
    GRPC_status status = GRPC_unary_blocking_call(chan, &method, context, msg, &resp);
    assert(status.code == GRPC_STATUS_OK);
    char *response_string = malloc(resp.length - 2 + 1);
    memcpy(response_string, ((char *) resp.data) + 2, resp.length - 2);
    response_string[resp.length - 2] = '\0';
    printf("Server said: %s\n", response_string);    // skip to the string in serialized protobuf object
    GRPC_message_destroy(&resp);
    GRPC_context_destroy(&context);
  }

  {
    printf("Testing async unary call\n");
    GRPC_method method = {NORMAL_RPC, "/helloworld.Greeter/SayHello"};
    GRPC_context *context = GRPC_context_create(chan);
    GRPC_completion_queue *cq = GRPC_completion_queue_create();
    // hardcoded string for "async gRPC-C"
    char str[] = {0x0A, 0x0C, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x20, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    GRPC_message resp;
    GRPC_client_async_response_reader *reader = GRPC_unary_async_call(chan, cq, method, msg, context);
    GRPC_client_async_finish(reader, &resp, (void*) 12345);
    printf("Waiting\n");
    void *tag;
    bool ok;
    GRPC_commit_ops_and_wait(cq, &tag, &ok);
    assert(ok);
    assert(tag == (void*) 12345);
    char *response_string = malloc(resp.length - 2 + 1);
    memcpy(response_string, ((char *) resp.data) + 2, resp.length - 2);
    response_string[resp.length - 2] = '\0';
    printf("Server said: %s\n", response_string);    // skip to the string in serialized protobuf object
    free(response_string);
    GRPC_message_destroy(&resp);
    GRPC_completion_queue_shutdown_and_destroy(cq);
    GRPC_context_destroy(&context);
  }

  {
    printf("Testing async unary call where the worker is in another thread\n");
    GRPC_completion_queue *cq = GRPC_completion_queue_create();

    pthread_t tid;
    pthread_create(&tid, NULL, async_say_hello_worker, cq);

    int i;
    for (i = 0; i < 10; i++) {
      async_say_hello(chan, cq);
    }

    printf("Waiting for thread to terminate\n");
    pthread_join(tid, NULL);

    GRPC_completion_queue_shutdown_and_destroy(cq);
  }

  {
    printf("Testing async unary call where the worker thread handles completion queue shutdown\n");
    GRPC_completion_queue *cq = GRPC_completion_queue_create();

    pthread_t tid;
    pthread_create(&tid, NULL, async_say_hello_worker, cq);

    int i;
    for (i = 0; i < 5; i++) {
      async_say_hello(chan, cq);
    }

    GRPC_completion_queue_shutdown(cq);
    printf("Waiting for thread to terminate\n");
    pthread_join(tid, NULL);
    GRPC_completion_queue_destroy(cq);
  }

  {
    printf("Testing blocking client streaming call\n");
    GRPC_method method = {NORMAL_RPC, "/helloworld.ClientStreamingGreeter/sayHello"};
    GRPC_context *context = GRPC_context_create(chan);
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    GRPC_message resp;

    GRPC_client_writer *writer = GRPC_client_streaming_blocking_call(chan, method, context, &resp);
    int i;
    for (i = 0; i < 3; i++) {
      GRPC_client_streaming_blocking_write(writer, msg);
    }
    GRPC_status status = GRPC_client_writer_terminate(writer);
    assert(status.code == GRPC_STATUS_OK);

    char *response_string = malloc(resp.length - 2 + 1);
    memcpy(response_string, ((char *) resp.data) + 2, resp.length - 2);
    response_string[resp.length - 2] = '\0';
    printf("Server said: %s\n", response_string);    // skip to the string in serialized protobuf object
    GRPC_message_destroy(&resp);
    GRPC_context_destroy(&context);
  }

  {
    printf("Testing blocking server streaming call\n");
    GRPC_method method = {NORMAL_RPC, "/helloworld.ServerStreamingGreeter/sayHello"};
    GRPC_context *context = GRPC_context_create(chan);
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};

    GRPC_client_reader *reader = GRPC_server_streaming_blocking_call(chan, method, context, msg);

    // using char array to hold RPC result while protobuf is not there yet
    GRPC_message resp;

    while (GRPC_server_streaming_blocking_read(reader, &resp)) {
      char *response_string = malloc(resp.length - 2 + 1);
      memcpy(response_string, ((char *) resp.data) + 2, resp.length - 2);
      response_string[resp.length - 2] = '\0';
      printf("Server said: %s\n", response_string);    // skip to the string in serialized protobuf object
      GRPC_message_destroy(&resp);
    }

    GRPC_status status = GRPC_client_reader_terminate(reader);
    assert(status.code == GRPC_STATUS_OK);

    GRPC_context_destroy(&context);
  }

  GRPC_channel_destroy(&chan);
  return 0;
}

typedef struct async_client {
  GRPC_context *context;
  GRPC_message reply;
} async_client;

static void async_say_hello(GRPC_channel *chan, GRPC_completion_queue *cq) {
  GRPC_method method = {NORMAL_RPC, "/helloworld.Greeter/SayHello"};
  GRPC_context *context = GRPC_context_create(chan);

  async_client *client = (async_client *) calloc(1, sizeof(async_client));
  client->context = context;

  // hardcoded string for "async gRPC-C"
  char str[] = {0x0A, 0x0C, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x20, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
  GRPC_message msg = {str, sizeof(str)};
  GRPC_client_async_response_reader *reader = GRPC_unary_async_call(chan, cq, method, msg, context);
  GRPC_client_async_finish(reader, &client->reply, client);
}

static void *async_say_hello_worker(void *param) {
  int i;
  GRPC_completion_queue *cq = (GRPC_completion_queue *) param;
  for (i = 0; i < 10; i++) {
    void *tag;
    bool ok;
    GRPC_completion_queue_operation_status status = GRPC_commit_ops_and_wait(cq, &tag, &ok);
    if (status == GRPC_COMPLETION_QUEUE_SHUTDOWN) {
      printf("Worker thread shutting down\n");
      return NULL;
    }
    assert(ok);
    assert(tag != NULL);
    async_client *client = (async_client *) tag;
    char *response_string = malloc(client->reply.length - 2 + 1);
    memcpy(response_string, ((char *) client->reply.data) + 2, client->reply.length - 2);
    response_string[client->reply.length - 2] = '\0';
    printf("Server said: %s\n", response_string);    // skip to the string in serialized protobuf object
    free(response_string);
    GRPC_message_destroy(&client->reply);
    GRPC_context_destroy(&client->context);
    free(client);
  }
  return NULL;
}
