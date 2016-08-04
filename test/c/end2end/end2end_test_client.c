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
 * This file contains the C part of end2end test. It is called by the GoogleTest-based C++ code.
 */

#include "test/c/end2end/end2end_test_client.h"
#include "src/proto/grpc/testing/echo.grpc.pbc.h"
#include <third_party/nanopb/pb_encode.h>
#include <third_party/nanopb/pb_decode.h>
#include <grpc/support/log.h>
#include <stdio.h>
#include <grpc_c/completion_queue.h>

/**
 * Nanopb callbacks for string encoding/decoding.
 */
static bool write_string_from_arg(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
  const char *str = *arg;
  if (!pb_encode_tag_for_field(stream, field))
    return false;

  return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

/**
 * This callback function reads a string from Nanopb stream and copies it into the callback args.
 * Users need to free the string after use.
 */
static bool read_string_store_in_arg(pb_istream_t *stream, const pb_field_t *field, void **arg) {
  size_t len = stream->bytes_left;
  char *str = malloc(len + 1);
  if (!pb_read(stream, (uint8_t *) str, len)) return false;
  str[len] = '\0';
  *arg = str;
  return true;
}

void test_client_send_unary_rpc(GRPC_channel *channel, int repeat) {
  int i;
  for (i = 0; i < repeat; i++) {
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
  }
}

void test_client_send_client_streaming_rpc(GRPC_channel *channel, int repeat) {
  int i;
  for (i = 0; i < repeat; i++) {
    grpc_testing_EchoRequest request = {.message = {.arg = "gRPC-C", .funcs.encode = write_string_from_arg}};
    grpc_testing_EchoResponse response = {.message = {.funcs.decode = read_string_store_in_arg}};

    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_client_writer *writer = grpc_testing_EchoTestService_RequestStream(context, &response);
    GPR_ASSERT(writer != NULL);
    int j;
    for (j = 0; j < 3; j++) {
      GPR_ASSERT(grpc_testing_EchoTestService_RequestStream_Write(writer, request));
    }
    GRPC_status status = grpc_testing_EchoTestService_RequestStream_Terminate(writer);
    GPR_ASSERT(status.ok);
    GPR_ASSERT(status.code == GRPC_STATUS_OK);
    GPR_ASSERT(response.message.arg != NULL);
    GPR_ASSERT(strcmp(response.message.arg, "gRPC-CgRPC-CgRPC-C") == 0);
    free(response.message.arg);
    GRPC_client_context_destroy(&context);
  }
}

void test_client_send_server_streaming_rpc(GRPC_channel *channel, int repeat) {
  int i;
  for (i = 0; i < repeat; i++) {
    grpc_testing_EchoRequest request = {.message = {.arg = "gRPC-C", .funcs.encode = write_string_from_arg}};
    grpc_testing_EchoResponse response = {.message = {.funcs.decode = read_string_store_in_arg}};

    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_client_reader *reader = grpc_testing_EchoTestService_ResponseStream(context, request);
    GPR_ASSERT(reader != NULL);
    int j = 0;
    while (grpc_testing_EchoTestService_ResponseStream_Read(reader, &response)) {
      GPR_ASSERT(response.message.arg != NULL);
      char *buf = malloc(strlen(response.message.arg) + 10);
      sprintf(buf, "%s%d", "gRPC-C", j);
      GPR_ASSERT(strcmp(buf, response.message.arg) == 0);
      free(buf);
      free(response.message.arg);
      j++;
    }
    GPR_ASSERT(j > 0);
    GRPC_status status = grpc_testing_EchoTestService_ResponseStream_Terminate(reader);
    GPR_ASSERT(status.ok);
    GPR_ASSERT(status.code == GRPC_STATUS_OK);
    GRPC_client_context_destroy(&context);
  }
}

void test_client_send_bidi_streaming_rpc(GRPC_channel *channel, int repeat) {
  int i;
  for (i = 0; i < repeat; i++) {
    grpc_testing_EchoRequest request = {.message = {.arg = "gRPC-C", .funcs.encode = write_string_from_arg}};
    grpc_testing_EchoResponse response = {.message = {.funcs.decode = read_string_store_in_arg}};

    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_client_reader_writer *reader_writer = grpc_testing_EchoTestService_BidiStream(context);
    GPR_ASSERT(reader_writer != NULL);

    const int kNumRequestToSend = 3;

    int j;
    for (j = 0; j < kNumRequestToSend; j++) {
      GPR_ASSERT(grpc_testing_EchoTestService_BidiStream_Write(reader_writer, request));
    }
    GPR_ASSERT(grpc_testing_EchoTestService_BidiStream_Writes_Done(reader_writer));

    int count = 0;
    while (grpc_testing_EchoTestService_BidiStream_Read(reader_writer, &response)) {
      GPR_ASSERT(response.message.arg != NULL);
      GPR_ASSERT(strcmp("gRPC-C", response.message.arg) == 0);
      free(response.message.arg);
      count++;
    }

    GPR_ASSERT(kNumRequestToSend == count);
    GRPC_status status = grpc_testing_EchoTestService_BidiStream_Terminate(reader_writer);
    GPR_ASSERT(status.ok);
    GPR_ASSERT(status.code == GRPC_STATUS_OK);
    GRPC_client_context_destroy(&context);
  }
}

void test_client_send_async_unary_rpc(GRPC_channel *channel, int repeat) {
  int i;
  for (i = 0; i < repeat; i++) {
    grpc_testing_EchoRequest request = {.message = {.arg = "gRPC-C", .funcs.encode = write_string_from_arg}};
    grpc_testing_EchoResponse response = {.message = {.funcs.decode = read_string_store_in_arg}};

    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_completion_queue *cq = GRPC_completion_queue_create();

    bool ok;
    void *tag;
    GRPC_client_async_response_reader *async_reader = grpc_testing_EchoTestService_Echo_Async(context, cq, request);
    GPR_ASSERT(async_reader != NULL);
    grpc_testing_EchoTestService_Echo_Finish(async_reader, &response, (void *) 12345);
    GRPC_completion_queue_next(cq, &tag, &ok);
    GPR_ASSERT(ok);
    GPR_ASSERT(tag == (void*) 12345);

    GRPC_status status = GRPC_get_call_status(context);
    GPR_ASSERT(status.ok);
    GPR_ASSERT(status.code == GRPC_STATUS_OK);
    GPR_ASSERT(response.message.arg != NULL);
    GPR_ASSERT(strcmp(response.message.arg, "gRPC-C") == 0);
    free(response.message.arg);

    GRPC_client_context_destroy(&context);
    GRPC_completion_queue_shutdown(cq);
    GRPC_completion_queue_shutdown_wait(cq);
    GRPC_completion_queue_destroy(cq);
  }
}
