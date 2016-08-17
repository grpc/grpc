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

#include <mutex>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/auth_metadata_processor.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

extern "C" {
#include <grpc_c/grpc_c.h>
#include <grpc_c/client_context.h>
#include <grpc_c/channel.h>
#include <include/grpc_c/codegen/unary_blocking_call.h>
#include <include/grpc_c/codegen/client_streaming_blocking_call.h>
#include <include/grpc_c/codegen/server_streaming_blocking_call.h>
#include <include/grpc_c/codegen/bidi_streaming_blocking_call.h>
#include <include/grpc_c/codegen/unary_async_call.h>
#include <grpc_c/status.h>
#include <grpc_c/codegen/context.h>
}

#include "src/core/lib/security/credentials/credentials.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_credentials_provider.h"
#include "test/c/end2end/id_serialization.h"

/**
 * End-to-end tests for the gRPC C API.
 * This test calls the codegen layer directly instead of exercising generated code.
 * As of early July 2016, this C API does not support creating servers, so we pull in a server implementation for C++
 * and put this test under the C++ build.
 * TODO(yifeit): Rewrite this in C after we have support for all types of API in server
 */

using grpc::testing::kTlsCredentialsType;
using std::chrono::system_clock;

namespace grpc {
namespace testing {
namespace {

class End2endTest {
public:
  End2endTest()
    : is_server_started_(false),
      kMaxMessageSize_(8192),
      c_channel_(NULL) {
  }

  ~End2endTest() {
    GRPC_channel_destroy(&c_channel_);
  }

  void TearDown() {
    if (is_server_started_) {
      server_->Shutdown();
    }
  }

  void StartServer(const std::shared_ptr<AuthMetadataProcessor> &processor) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "127.0.0.1:" << port;
    // Setup server
    ServerBuilder builder;

    builder.AddListeningPort(server_address_.str(), InsecureServerCredentials());
    builder.RegisterService(&service_);
    builder.SetMaxMessageSize(
      kMaxMessageSize_);  // For testing max message size.
    server_ = builder.BuildAndStart();
    is_server_started_ = true;
  }

  void ResetChannel() {
    if (!is_server_started_) {
      StartServer(std::shared_ptr<AuthMetadataProcessor>());
    }
    EXPECT_TRUE(is_server_started_);

    // TODO(yifeit): add credentials
    if (c_channel_) GRPC_channel_destroy(&c_channel_);
    c_channel_ = GRPC_channel_create(server_address_.str().c_str());
  }

  void ResetStub() {
    ResetChannel();
  }

  bool is_server_started_;

  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  const int kMaxMessageSize_;
  TestServiceImpl service_;
  grpc::string user_agent_prefix_;

  GRPC_channel *c_channel_;
};

static void SendUnaryRpc(GRPC_channel *channel,
                    int num_rpcs) {
  for (int i = 0; i < num_rpcs; ++i) {
    GRPC_method method = { GRPC_NORMAL_RPC, "/grpc.testing.EchoTestService/Echo" };
    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_context_set_serialization_impl((GRPC_context *) context, { GRPC_id_serialize, GRPC_id_deserialize });
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    char resp[100];
    GRPC_status status = GRPC_unary_blocking_call(method, context, msg, resp);

    EXPECT_TRUE(status.ok) << status.details;
    EXPECT_TRUE(status.code == GRPC_STATUS_OK) << status.details;

    // manually deserializing
    int resplength = (int) resp[1];
    char *response_string = new char[resplength + 1];
    memcpy(response_string, ((char *) resp) + 2, resplength);
    response_string[resplength] = '\0';

    EXPECT_EQ(grpc::string("gRPC-C"), grpc::string(response_string));

    delete []response_string;
    GRPC_client_context_destroy(&context);
  }
}

static void SendClientStreamingRpc(GRPC_channel *channel,
                         int num_rpcs) {
  for (int i = 0; i < num_rpcs; ++i) {
    GRPC_method method = { GRPC_CLIENT_STREAMING, "/grpc.testing.EchoTestService/RequestStream" };
    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_context_set_serialization_impl((GRPC_context *) context, { GRPC_id_serialize, GRPC_id_deserialize });
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    char resp[100];

    GRPC_client_writer *writer = GRPC_client_streaming_blocking_call(method, context, resp);
    for (int i = 0; i < 3; i++) {
      bool result = GRPC_client_streaming_blocking_write(writer, msg);
      EXPECT_TRUE(result);
    }
    GRPC_status status = GRPC_client_writer_terminate(writer);

    EXPECT_TRUE(status.ok) << status.details;
    EXPECT_TRUE(status.code == GRPC_STATUS_OK) << status.details;

    // manually deserializing
    int resplength = (int) resp[1];
    char *response_string = new char[resplength + 1];
    memcpy(response_string, ((char *) resp) + 2, resplength);
    response_string[resplength] = '\0';

    EXPECT_EQ(grpc::string("gRPC-CgRPC-CgRPC-C"), grpc::string(response_string));

    delete []response_string;
    GRPC_client_context_destroy(&context);
  }
}

static void SendServerStreamingRpc(GRPC_channel *channel,
                                   int num_rpcs) {
  for (int i = 0; i < num_rpcs; ++i) {
    GRPC_method method = { GRPC_SERVER_STREAMING, "/grpc.testing.EchoTestService/ResponseStream" };
    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_context_set_serialization_impl((GRPC_context *) context, { GRPC_id_serialize, GRPC_id_deserialize });
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};

    GRPC_client_reader *reader = GRPC_server_streaming_blocking_call(method, context, msg);

    // using char array to hold RPC result while protobuf is not there yet
    char resp[100];

    int count = 0;
    while (GRPC_server_streaming_blocking_read(reader, resp)) {
      // manually deserializing
      int resplength = (int) resp[1];
      char *response_string = new char[resplength + 1];
      memcpy(response_string, ((char *) resp) + 2, resplength);
      response_string[resplength] = '\0';
      EXPECT_EQ(grpc::string("gRPC-C") + grpc::to_string(count), grpc::string(response_string));
      count++;
      delete []response_string;
    }
    EXPECT_TRUE(count > 0);

    GRPC_status status = GRPC_client_reader_terminate(reader);
    EXPECT_TRUE(status.ok) << status.details;
    EXPECT_TRUE(status.code == GRPC_STATUS_OK) << status.details;

    GRPC_client_context_destroy(&context);
  }
}

static void SendBidiStreamingRpc(GRPC_channel *channel,
                                   int num_rpcs) {
  for (int i = 0; i < num_rpcs; ++i) {
    GRPC_method method = { GRPC_BIDI_STREAMING, "/grpc.testing.EchoTestService/BidiStream" };
    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_context_set_serialization_impl((GRPC_context *) context, { GRPC_id_serialize, GRPC_id_deserialize });
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};

    GRPC_client_reader_writer *reader_writer = GRPC_bidi_streaming_blocking_call(method, context);

    // using char array to hold RPC result while protobuf is not there yet
    char resp[100];

    const int kNumMsgToSend = 3;
    for (int i = 0; i < kNumMsgToSend; i++) {
      EXPECT_TRUE(GRPC_bidi_streaming_blocking_write(reader_writer, msg));
    }
    EXPECT_TRUE(GRPC_bidi_streaming_blocking_writes_done(reader_writer));

    int received_num = 0;
    while (GRPC_bidi_streaming_blocking_read(reader_writer, resp)) {
      received_num++;
      // manually deserializing
      int resplength = (int) resp[1];
      char *response_string = new char[resplength + 1];
      memcpy(response_string, ((char *) resp) + 2, resplength);
      response_string[resplength] = '\0';
      EXPECT_EQ(grpc::string("gRPC-C"), grpc::string(response_string));
      delete []response_string;
    }
    EXPECT_EQ(kNumMsgToSend, received_num);

    GRPC_status status = GRPC_client_reader_writer_terminate(reader_writer);
    EXPECT_TRUE(status.ok) << status.details;
    EXPECT_TRUE(status.code == GRPC_STATUS_OK) << status.details;

    GRPC_client_context_destroy(&context);
  }
}

static void SendAsyncUnaryRpc(GRPC_channel *channel,
                         int num_rpcs) {
  for (int i = 0; i < num_rpcs; ++i) {
    GRPC_method method = { GRPC_NORMAL_RPC, "/grpc.testing.EchoTestService/Echo" };
    GRPC_client_context *context = GRPC_client_context_create(channel);
    GRPC_context_set_serialization_impl((GRPC_context *) context, { GRPC_id_serialize, GRPC_id_deserialize });
    GRPC_completion_queue *cq = GRPC_completion_queue_create();
    // hardcoded string for "gRPC-C"
    char str[] = {0x0A, 0x06, 0x67, 0x52, 0x50, 0x43, 0x2D, 0x43};
    GRPC_message msg = {str, sizeof(str)};
    // using char array to hold RPC result while protobuf is not there yet
    char resp[100];

    void *tag;
    bool ok;
    GRPC_client_async_response_reader *reader = GRPC_unary_async_call(cq, method, msg, context);
    GRPC_client_async_finish(reader, resp, (void*) 12345);
    GRPC_completion_queue_next(cq, &tag, &ok);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(tag == (void*) 12345);

    GRPC_status status = GRPC_get_call_status(context);
    EXPECT_TRUE(status.ok) << status.details;
    EXPECT_TRUE(status.code == GRPC_STATUS_OK) << status.details;

    // manually deserializing
    int resplength = (int) resp[1];
    char *response_string = new char[resplength + 1];
    memcpy(response_string, ((char *) resp) + 2, resplength);
    response_string[resplength] = '\0';

    EXPECT_EQ(grpc::string("gRPC-C"), grpc::string(response_string));

    delete []response_string;
    GRPC_client_context_destroy(&context);
    GRPC_completion_queue_shutdown(cq);
    GRPC_completion_queue_shutdown_wait(cq);
    GRPC_completion_queue_destroy(cq);
  }
}

class UnaryEnd2endTest : public End2endTest {
protected:
};

class ClientStreamingEnd2endTest : public End2endTest {
protected:
};

class ServerStreamingEnd2endTest : public End2endTest {
protected:
};

class BidiStreamingEnd2endTest : public End2endTest {
protected:
};

class AsyncUnaryEnd2endTest : public End2endTest {
protected:
};

TEST(End2endTest, UnaryRpc) {
  UnaryEnd2endTest test;
  test.ResetStub();
  SendUnaryRpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, ClientStreamingRpc) {
  ClientStreamingEnd2endTest test;
  test.ResetStub();
  SendClientStreamingRpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, ServerStreamingRpc) {
  ServerStreamingEnd2endTest test;
  test.ResetStub();
  SendServerStreamingRpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, BidiStreamingRpc) {
  BidiStreamingEnd2endTest test;
  test.ResetStub();
  SendBidiStreamingRpc(test.c_channel_, 3);
  test.TearDown();
}

TEST(End2endTest, AsyncUnaryRpc) {
  AsyncUnaryEnd2endTest test;
  test.ResetStub();
  SendAsyncUnaryRpc(test.c_channel_, 3);
  test.TearDown();
}

} // namespace
} // namespace testing
} // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
