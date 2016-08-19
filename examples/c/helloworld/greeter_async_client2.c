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
 * This example shows the async unary API where there are multiple worker
 * threads processing RPCs.
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "helloworld.grpc.pbc.h"

typedef struct async_client {
  GRPC_client_context *context;
  helloworld_HelloReply reply;
} async_client;

pthread_mutex_t num_responses_lock;
int num_responses;

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

static void async_say_hello(GRPC_channel *chan, GRPC_completion_queue *cq) {
  GRPC_client_context *context = GRPC_client_context_create(chan);

  async_client *client = (async_client *)calloc(1, sizeof(async_client));
  client->context = context;
  client->reply.message.funcs.decode = read_string;

  helloworld_HelloRequest request = {.name.funcs.encode = write_string};
  GRPC_client_async_response_reader *reader =
      helloworld_Greeter_SayHello_Async(context, cq, request);
  helloworld_Greeter_SayHello_Finish(reader, &client->reply, client);
}

static void *async_say_hello_worker(void *param) {
  int i;
  GRPC_completion_queue *cq = (GRPC_completion_queue *)param;
  for (;;) {
    void *tag;
    bool ok;
    GRPC_completion_queue_operation_status op_status =
        GRPC_completion_queue_next(cq, &tag, &ok);
    if (op_status == GRPC_COMPLETION_QUEUE_SHUTDOWN) {
      printf("Worker thread shutting down\n");
      return NULL;
    }

    // Increment response count
    pthread_mutex_lock(&num_responses_lock);
    num_responses++;
    pthread_mutex_unlock(&num_responses_lock);

    assert(ok);
    assert(tag != NULL);
    async_client *client = (async_client *)tag;
    GRPC_status status = GRPC_get_call_status(client->context);
    assert(status.ok);
    assert(status.code == GRPC_STATUS_OK);
    GRPC_client_context_destroy(&client->context);
    free(client);
  }
  return NULL;
}

int main(int argc, char **argv) {
  GRPC_channel *chan = GRPC_channel_create("0.0.0.0:50051");
  GRPC_completion_queue *cq = GRPC_completion_queue_create();

  num_responses = 0;
  pthread_mutex_init(&num_responses_lock, NULL);

/* Start worker threads */
#define THREAD_COUNT 3
  pthread_t tids[THREAD_COUNT];
  int i;
  for (i = 0; i < THREAD_COUNT; i++) {
    pthread_create(&tids[i], NULL, async_say_hello_worker, cq);
  }

  for (i = 0; i < 100; i++) {
    async_say_hello(chan, cq);
  }

  GRPC_completion_queue_shutdown(cq);

  printf("Waiting for thread to terminate\n");
  for (i = 0; i < THREAD_COUNT; i++) {
    pthread_join(tids[i], NULL);
  }

  GRPC_completion_queue_destroy(cq);
  GRPC_channel_destroy(&chan);

  printf("Total number of responses: %d\n", num_responses);
  pthread_mutex_destroy(&num_responses_lock);

  return 0;
}
