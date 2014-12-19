/*
 *
 * Copyright 2014, Google Inc.
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


#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include "src/cpp/client/internal_stub.h"
#include "src/cpp/rpc_method.h"
#include "test/cpp/util/echo.pb.h"
#include "net/util/netutil.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/cpp/end2end/async_test_server.h"
#include <gtest/gtest.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

using grpc::testing::AsyncTestServer;

namespace grpc {
namespace {

void ServerLoop(void* s) {
  AsyncTestServer* server = static_cast<AsyncTestServer*>(s);
  server->MainLoop();
}

class End2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = PickUnusedPortOrDie();
    // TODO(yangg) protobuf has a StringPrintf, maybe use that
    std::ostringstream oss;
    oss << "[::]:" << port;
    // Setup server
    server_.reset(new AsyncTestServer());
    server_->AddPort(oss.str());
    server_->Start();

    RunServerThread();

    // Setup client
    oss.str("");
    oss << "127.0.0.1:" << port;
    std::shared_ptr<ChannelInterface> channel =
        CreateChannel(oss.str(), ChannelArguments());
    stub_.set_channel(channel);
  }

  void RunServerThread() {
    gpr_thd_id id;
    EXPECT_TRUE(gpr_thd_new(&id, ServerLoop, server_.get(), NULL));
  }

  void TearDown() override {
    server_->Shutdown();
  }

  std::unique_ptr<AsyncTestServer> server_;
  InternalStub stub_;
};

TEST_F(End2endTest, NoOpTest) { EXPECT_TRUE(stub_.channel() != nullptr); }

TEST_F(End2endTest, SimpleRpc) {
  EchoRequest request;
  request.set_message("hello");
  EchoResponse result;
  ClientContext context;
  RpcMethod method("/foo");
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(10);
  context.set_absolute_deadline(deadline);
  Status s =
      stub_.channel()->StartBlockingRpc(method, &context, request, &result);
  EXPECT_EQ(result.message(), request.message());
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, KSequentialSimpleRpcs) {
  int k = 3;
  for (int i = 0; i < k; i++) {
    EchoRequest request;
    request.set_message("hello");
    EchoResponse result;
    ClientContext context;
    RpcMethod method("/foo");
    std::chrono::system_clock::time_point deadline =
        std::chrono::system_clock::now() + std::chrono::seconds(10);
    context.set_absolute_deadline(deadline);
    Status s =
        stub_.channel()->StartBlockingRpc(method, &context, request, &result);
    EXPECT_EQ(result.message(), request.message());
    EXPECT_TRUE(s.IsOk());
  }
}

TEST_F(End2endTest, OnePingpongBidiStream) {
  EchoRequest request;
  request.set_message("hello");
  EchoResponse result;
  ClientContext context;
  RpcMethod method("/foo", RpcMethod::RpcType::BIDI_STREAMING);
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(10);
  context.set_absolute_deadline(deadline);
  StreamContextInterface* stream_interface =
      stub_.channel()->CreateStream(method, &context, nullptr, nullptr);
  std::unique_ptr<ClientReaderWriter<EchoRequest, EchoResponse>> stream(
      new ClientReaderWriter<EchoRequest, EchoResponse>(stream_interface));
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&result));
  stream->WritesDone();
  EXPECT_FALSE(stream->Read(&result));
  Status s = stream->Wait();
  EXPECT_EQ(result.message(), request.message());
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, TwoPingpongBidiStream) {
  EchoRequest request;
  request.set_message("hello");
  EchoResponse result;
  ClientContext context;
  RpcMethod method("/foo", RpcMethod::RpcType::BIDI_STREAMING);
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(10);
  context.set_absolute_deadline(deadline);
  StreamContextInterface* stream_interface =
      stub_.channel()->CreateStream(method, &context, nullptr, nullptr);
  std::unique_ptr<ClientReaderWriter<EchoRequest, EchoResponse>> stream(
      new ClientReaderWriter<EchoRequest, EchoResponse>(stream_interface));
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&result));
  EXPECT_EQ(result.message(), request.message());
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&result));
  EXPECT_EQ(result.message(), request.message());
  stream->WritesDone();
  EXPECT_FALSE(stream->Read(&result));
  Status s = stream->Wait();
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, OnePingpongClientStream) {
  EchoRequest request;
  request.set_message("hello");
  EchoResponse result;
  ClientContext context;
  RpcMethod method("/foo", RpcMethod::RpcType::CLIENT_STREAMING);
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(10);
  context.set_absolute_deadline(deadline);
  StreamContextInterface* stream_interface =
      stub_.channel()->CreateStream(method, &context, nullptr, &result);
  std::unique_ptr<ClientWriter<EchoRequest>> stream(
      new ClientWriter<EchoRequest>(stream_interface));
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Wait();
  EXPECT_EQ(result.message(), request.message());
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, OnePingpongServerStream) {
  EchoRequest request;
  request.set_message("hello");
  EchoResponse result;
  ClientContext context;
  RpcMethod method("/foo", RpcMethod::RpcType::SERVER_STREAMING);
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(10);
  context.set_absolute_deadline(deadline);
  StreamContextInterface* stream_interface =
      stub_.channel()->CreateStream(method, &context, &request, nullptr);
  std::unique_ptr<ClientReader<EchoResponse>> stream(
      new ClientReader<EchoResponse>(stream_interface));
  EXPECT_TRUE(stream->Read(&result));
  EXPECT_FALSE(stream->Read(nullptr));
  Status s = stream->Wait();
  EXPECT_EQ(result.message(), request.message());
  EXPECT_TRUE(s.IsOk());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
