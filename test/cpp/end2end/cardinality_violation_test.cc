//
//
// Copyright 2026 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

inline void* tag(int i) {
  return reinterpret_cast<void*>(static_cast<intptr_t>(i));
}

class TestServiceImpl : public EchoTestService::Service {
 public:
  Status Echo(ServerContext* /*context*/, const EchoRequest* /*request*/,
              EchoResponse* response) override {
    response->set_message("response");
    return Status::OK;
  }

  Status RequestStream(ServerContext* /*context*/,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override {
    EchoRequest request;
    while (reader->Read(&request)) {
    }
    response->set_message("response");
    return Status::OK;
  }

  Status ResponseStream(ServerContext* /*context*/,
                        const EchoRequest* /*request*/,
                        ServerWriter<EchoResponse>* writer) override {
    EchoResponse response;
    response.set_message("response");
    writer->Write(response);
    return Status::OK;
  }
};

class CardinalityViolationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = 0;
    ServerBuilder builder;
    builder.AddListeningPort("localhost:0", InsecureServerCredentials(), &port);
    builder.RegisterService(&service_);
    builder.RegisterAsyncGenericService(&generic_service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    server_address_ << "localhost:" << port;

    channel_ = grpc::CreateCustomChannel(server_address_.str(),
                                         InsecureChannelCredentials(),
                                         ChannelArguments());
  }

  void TearDown() override { server_->Shutdown(); }

  std::shared_ptr<Channel> channel_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
  AsyncGenericService generic_service_;
};

// Verifies Server Case 1: For a Unary RPC, if no request message is sent by the
// client and the stream is half-closed, the server should report an
// UNIMPLEMENTED status instead of an INTERNAL error.
TEST_F(CardinalityViolationTest, UnaryZeroRequests) {
  ClientContext context;
  Status status;
  GenericStub generic_stub(channel_);
  CompletionQueue cq;
  std::unique_ptr<GenericClientAsyncReaderWriter> call =
      generic_stub.PrepareCall(&context, "/grpc.testing.EchoTestService/Echo",
                               &cq);
  call->StartCall(tag(1));
  void* got_tag;
  bool ok;
  EXPECT_TRUE(cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(1));
  call->WritesDone(tag(2));
  EXPECT_TRUE(cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(2));
  call->Finish(&status, tag(3));
  EXPECT_TRUE(cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(3));
  EXPECT_EQ(status.error_code(), StatusCode::UNIMPLEMENTED);
}

TEST_F(CardinalityViolationTest, ServerStreamingZeroRequests) {
  ClientContext context;
  Status status;
  GenericStub generic_stub(channel_);
  CompletionQueue cq;
  std::unique_ptr<GenericClientAsyncReaderWriter> call =
      generic_stub.PrepareCall(
          &context, "/grpc.testing.EchoTestService/ResponseStream", &cq);
  call->StartCall(tag(1));
  void* got_tag;
  bool ok;
  EXPECT_TRUE(cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(1));
  call->WritesDone(tag(2));
  EXPECT_TRUE(cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(2));
  call->Finish(&status, tag(3));
  EXPECT_TRUE(cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(3));
  EXPECT_EQ(status.error_code(), StatusCode::UNIMPLEMENTED);
}

TEST_F(CardinalityViolationTest, ClientStreamingZeroResponses) {
  ClientContext context;
  EchoResponse response;
  grpc::internal::RpcMethod method(
      "/grpc.testing.EchoTestService/UnregisteredClientStreamingMethod",
      grpc::internal::RpcMethod::CLIENT_STREAMING);
  auto writer = grpc::internal::ClientWriterFactory<EchoRequest>::Create(
      channel_.get(), method, &context, &response);
  GenericServerContext server_context;
  GenericServerAsyncReaderWriter stream(&server_context);
  generic_service_.RequestCall(&server_context, &stream, cq_.get(), cq_.get(),
                               tag(100));
  void* got_tag;
  bool ok;
  EXPECT_TRUE(cq_->Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(100));
  stream.Finish(Status::OK, tag(101));
  EXPECT_TRUE(cq_->Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(101));
  writer->WritesDone();
  Status s = writer->Finish();
  EXPECT_EQ(s.error_code(), StatusCode::UNIMPLEMENTED);
  delete writer;
}

TEST_F(CardinalityViolationTest, ClientAsyncStreamingZeroResponses) {
  ClientContext context;
  EchoResponse response;
  grpc::internal::RpcMethod method(
      "/grpc.testing.EchoTestService/UnregisteredClientAsyncStreamingMethod",
      grpc::internal::RpcMethod::CLIENT_STREAMING);
  CompletionQueue client_cq;
  auto writer = grpc::internal::ClientAsyncWriterFactory<EchoRequest>::Create(
      channel_.get(), &client_cq, method, &context, &response, true, tag(1));
  void* got_tag;
  bool ok;
  EXPECT_TRUE(client_cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(1));
  GenericServerContext server_context;
  GenericServerAsyncReaderWriter stream(&server_context);
  generic_service_.RequestCall(&server_context, &stream, cq_.get(), cq_.get(),
                               tag(100));
  EXPECT_TRUE(cq_->Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(100));
  stream.Finish(Status::OK, tag(101));
  EXPECT_TRUE(cq_->Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(101));
  writer->WritesDone(tag(2));
  EXPECT_TRUE(client_cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(2));
  Status s;
  writer->Finish(&s, tag(3));
  EXPECT_TRUE(client_cq.Next(&got_tag, &ok));
  EXPECT_EQ(got_tag, tag(3));
  EXPECT_EQ(s.error_code(), StatusCode::UNIMPLEMENTED);
  delete writer;
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
