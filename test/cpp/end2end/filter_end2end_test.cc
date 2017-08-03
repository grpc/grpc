/*
 *
 * Copyright 2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <memory>
#include <mutex>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/generic/async_generic_service.h>
#include <grpc++/generic/generic_stub.h>
#include <grpc++/impl/codegen/proto_utils.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/support/config.h>
#include <grpc++/support/slice.h>
#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#include "src/cpp/common/channel_filter.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using std::chrono::system_clock;

namespace grpc {
namespace testing {
namespace {

void* tag(int i) { return (void*)(intptr_t)i; }

void verify_ok(CompletionQueue* cq, int i, bool expect_ok) {
  bool ok;
  void* got_tag;
  EXPECT_TRUE(cq->Next(&got_tag, &ok));
  EXPECT_EQ(expect_ok, ok);
  EXPECT_EQ(tag(i), got_tag);
}

namespace {

int global_num_connections = 0;
int global_num_calls = 0;
std::mutex global_mu;

void IncrementConnectionCounter() {
  std::unique_lock<std::mutex> lock(global_mu);
  ++global_num_connections;
}

void ResetConnectionCounter() {
  std::unique_lock<std::mutex> lock(global_mu);
  global_num_connections = 0;
}

int GetConnectionCounterValue() {
  std::unique_lock<std::mutex> lock(global_mu);
  return global_num_connections;
}

void IncrementCallCounter() {
  std::unique_lock<std::mutex> lock(global_mu);
  ++global_num_calls;
}

void ResetCallCounter() {
  std::unique_lock<std::mutex> lock(global_mu);
  global_num_calls = 0;
}

int GetCallCounterValue() {
  std::unique_lock<std::mutex> lock(global_mu);
  return global_num_calls;
}

}  // namespace

class ChannelDataImpl : public ChannelData {
 public:
  grpc_error* Init(grpc_exec_ctx* exec_ctx, grpc_channel_element* elem,
                   grpc_channel_element_args* args) {
    IncrementConnectionCounter();
    return GRPC_ERROR_NONE;
  }
};

class CallDataImpl : public CallData {
 public:
  void StartTransportStreamOpBatch(grpc_exec_ctx* exec_ctx,
                                   grpc_call_element* elem,
                                   TransportStreamOpBatch* op) override {
    // Incrementing the counter could be done from Init(), but we want
    // to test that the individual methods are actually called correctly.
    if (op->recv_initial_metadata() != nullptr) IncrementCallCounter();
    grpc_call_next_op(exec_ctx, elem, op->op());
  }
};

class FilterEnd2endTest : public ::testing::Test {
 protected:
  FilterEnd2endTest() : server_host_("localhost") {}

  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << server_host_ << ":" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterAsyncGenericService(&generic_service_);
    srv_cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
  }

  void TearDown() override {
    server_->Shutdown();
    void* ignored_tag;
    bool ignored_ok;
    cli_cq_.Shutdown();
    srv_cq_->Shutdown();
    while (cli_cq_.Next(&ignored_tag, &ignored_ok))
      ;
    while (srv_cq_->Next(&ignored_tag, &ignored_ok))
      ;
  }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    generic_stub_.reset(new GenericStub(channel));
    ResetConnectionCounter();
    ResetCallCounter();
  }

  void server_ok(int i) { verify_ok(srv_cq_.get(), i, true); }
  void client_ok(int i) { verify_ok(&cli_cq_, i, true); }
  void server_fail(int i) { verify_ok(srv_cq_.get(), i, false); }
  void client_fail(int i) { verify_ok(&cli_cq_, i, false); }

  void SendRpc(int num_rpcs) {
    const grpc::string kMethodName("/grpc.cpp.test.util.EchoTestService/Echo");
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest send_request;
      EchoRequest recv_request;
      EchoResponse send_response;
      EchoResponse recv_response;
      Status recv_status;

      ClientContext cli_ctx;
      GenericServerContext srv_ctx;
      GenericServerAsyncReaderWriter stream(&srv_ctx);

      // The string needs to be long enough to test heap-based slice.
      send_request.set_message("Hello world. Hello world. Hello world.");
      std::unique_ptr<GenericClientAsyncReaderWriter> call =
          generic_stub_->Call(&cli_ctx, kMethodName, &cli_cq_, tag(1));
      client_ok(1);
      std::unique_ptr<ByteBuffer> send_buffer =
          SerializeToByteBuffer(&send_request);
      call->Write(*send_buffer, tag(2));
      // Send ByteBuffer can be destroyed after calling Write.
      send_buffer.reset();
      client_ok(2);
      call->WritesDone(tag(3));
      client_ok(3);

      generic_service_.RequestCall(&srv_ctx, &stream, srv_cq_.get(),
                                   srv_cq_.get(), tag(4));

      verify_ok(srv_cq_.get(), 4, true);
      EXPECT_EQ(server_host_, srv_ctx.host().substr(0, server_host_.length()));
      EXPECT_EQ(kMethodName, srv_ctx.method());
      ByteBuffer recv_buffer;
      stream.Read(&recv_buffer, tag(5));
      server_ok(5);
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
      EXPECT_EQ(send_request.message(), recv_request.message());

      send_response.set_message(recv_request.message());
      send_buffer = SerializeToByteBuffer(&send_response);
      stream.Write(*send_buffer, tag(6));
      send_buffer.reset();
      server_ok(6);

      stream.Finish(Status::OK, tag(7));
      server_ok(7);

      recv_buffer.Clear();
      call->Read(&recv_buffer, tag(8));
      client_ok(8);
      EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_response));

      call->Finish(&recv_status, tag(9));
      client_ok(9);

      EXPECT_EQ(send_response.message(), recv_response.message());
      EXPECT_TRUE(recv_status.ok());
    }
  }

  CompletionQueue cli_cq_;
  std::unique_ptr<ServerCompletionQueue> srv_cq_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<grpc::GenericStub> generic_stub_;
  std::unique_ptr<Server> server_;
  AsyncGenericService generic_service_;
  const grpc::string server_host_;
  std::ostringstream server_address_;
};

TEST_F(FilterEnd2endTest, SimpleRpc) {
  ResetStub();
  EXPECT_EQ(0, GetConnectionCounterValue());
  EXPECT_EQ(0, GetCallCounterValue());
  SendRpc(1);
  EXPECT_EQ(1, GetConnectionCounterValue());
  EXPECT_EQ(1, GetCallCounterValue());
}

TEST_F(FilterEnd2endTest, SequentialRpcs) {
  ResetStub();
  EXPECT_EQ(0, GetConnectionCounterValue());
  EXPECT_EQ(0, GetCallCounterValue());
  SendRpc(10);
  EXPECT_EQ(1, GetConnectionCounterValue());
  EXPECT_EQ(10, GetCallCounterValue());
}

// One ping, one pong.
TEST_F(FilterEnd2endTest, SimpleBidiStreaming) {
  ResetStub();
  EXPECT_EQ(0, GetConnectionCounterValue());
  EXPECT_EQ(0, GetCallCounterValue());

  const grpc::string kMethodName(
      "/grpc.cpp.test.util.EchoTestService/BidiStream");
  EchoRequest send_request;
  EchoRequest recv_request;
  EchoResponse send_response;
  EchoResponse recv_response;
  Status recv_status;
  ClientContext cli_ctx;
  GenericServerContext srv_ctx;
  GenericServerAsyncReaderWriter srv_stream(&srv_ctx);

  cli_ctx.set_compression_algorithm(GRPC_COMPRESS_GZIP);
  send_request.set_message("Hello");
  std::unique_ptr<GenericClientAsyncReaderWriter> cli_stream =
      generic_stub_->Call(&cli_ctx, kMethodName, &cli_cq_, tag(1));
  client_ok(1);

  generic_service_.RequestCall(&srv_ctx, &srv_stream, srv_cq_.get(),
                               srv_cq_.get(), tag(2));

  verify_ok(srv_cq_.get(), 2, true);
  EXPECT_EQ(server_host_, srv_ctx.host().substr(0, server_host_.length()));
  EXPECT_EQ(kMethodName, srv_ctx.method());

  std::unique_ptr<ByteBuffer> send_buffer =
      SerializeToByteBuffer(&send_request);
  cli_stream->Write(*send_buffer, tag(3));
  send_buffer.reset();
  client_ok(3);

  ByteBuffer recv_buffer;
  srv_stream.Read(&recv_buffer, tag(4));
  server_ok(4);
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
  EXPECT_EQ(send_request.message(), recv_request.message());

  send_response.set_message(recv_request.message());
  send_buffer = SerializeToByteBuffer(&send_response);
  srv_stream.Write(*send_buffer, tag(5));
  send_buffer.reset();
  server_ok(5);

  cli_stream->Read(&recv_buffer, tag(6));
  client_ok(6);
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_response));
  EXPECT_EQ(send_response.message(), recv_response.message());

  cli_stream->WritesDone(tag(7));
  client_ok(7);

  srv_stream.Read(&recv_buffer, tag(8));
  server_fail(8);

  srv_stream.Finish(Status::OK, tag(9));
  server_ok(9);

  cli_stream->Finish(&recv_status, tag(10));
  client_ok(10);

  EXPECT_EQ(send_response.message(), recv_response.message());
  EXPECT_TRUE(recv_status.ok());

  EXPECT_EQ(1, GetCallCounterValue());
  EXPECT_EQ(1, GetConnectionCounterValue());
}

void RegisterFilter() {
  grpc::RegisterChannelFilter<ChannelDataImpl, CallDataImpl>(
      "test-filter", GRPC_SERVER_CHANNEL, INT_MAX, nullptr);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::RegisterFilter();
  return RUN_ALL_TESTS();
}
