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
#include <thread>

#include "test/cpp/util/echo.pb.h"
#include "src/cpp/server/rpc_service_method.h"
#include "src/cpp/util/time.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "net/util/netutil.h"
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
using grpc::cpp::test::util::TestService;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

namespace {

// When echo_deadline is requested, deadline seen in the ServerContext is set in
// the response in seconds.
void MaybeEchoDeadline(ServerContext* context, const EchoRequest* request,
                       EchoResponse* response) {
  if (request->has_param() && request->param().echo_deadline()) {
    gpr_timespec deadline = gpr_inf_future;
    if (context->absolute_deadline() != system_clock::time_point::max()) {
      Timepoint2Timespec(context->absolute_deadline(), &deadline);
    }
    response->mutable_param()->set_request_deadline(deadline.tv_sec);
  }
}
}  // namespace

class TestServiceImpl : public TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) {
    response->set_message(request->message());
    MaybeEchoDeadline(context, request, response);
    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) {
    EchoRequest request;
    response->set_message("");
    while (reader->Read(&request)) {
      response->mutable_message()->append(request.message());
    }
    return Status::OK;
  }

  // Return 3 messages.
  // TODO(yangg) make it generic by adding a parameter into EchoRequest
  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) {
    EchoResponse response;
    response.set_message(request->message() + "0");
    writer->Write(response);
    response.set_message(request->message() + "1");
    writer->Write(response);
    response.set_message(request->message() + "2");
    writer->Write(response);

    return Status::OK;
  }

  Status BidiStream(ServerContext* context,
                    ServerReaderWriter<EchoResponse, EchoRequest>* stream) {
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      response.set_message(request.message());
      stream->Write(response);
    }
    return Status::OK;
  }
};

class End2endTest : public ::testing::Test {
 protected:
  void SetUp() override {
    int port = PickUnusedPortOrDie();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddPort(server_address_.str());
    builder.RegisterService(service_.service());
    server_ = builder.BuildAndStart();
  }

  void TearDown() override {
    server_->Shutdown();
  }

  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
};

static void SendRpc(const grpc::string& server_address, int num_rpcs) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address, ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    Status s = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    EXPECT_TRUE(s.IsOk());
  }

  delete stub;
}

TEST_F(End2endTest, SimpleRpc) {
  SendRpc(server_address_.str(), 1);
}

TEST_F(End2endTest, MultipleRpcs) {
  vector<std::thread*> threads;
  for (int i = 0; i < 10; ++i) {
    threads.push_back(new std::thread(SendRpc, server_address_.str(), 10));
  }
  for (int i = 0; i < 10; ++i) {
    threads[i]->join();
    delete threads[i];
  }
}

// Set a 10us deadline and make sure proper error is returned.
TEST_F(End2endTest, RpcDeadlineExpires) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::microseconds(10);
  context.set_absolute_deadline(deadline);
  Status s = stub->Echo(&context, request, &response);
  // TODO(yangg) use correct error code when b/18793983 is fixed.
  // EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, s.code());
  EXPECT_EQ(StatusCode::CANCELLED, s.code());

  delete stub;
}

// Set a long but finite deadline.
TEST_F(End2endTest, RpcLongDeadline) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::hours(1);
  context.set_absolute_deadline(deadline);
  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());

  delete stub;
}

// Ask server to echo back the deadline it sees.
TEST_F(End2endTest, EchoDeadline) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(100);
  context.set_absolute_deadline(deadline);
  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());
  gpr_timespec sent_deadline;
  Timepoint2Timespec(deadline, &sent_deadline);
  // Allow 1 second error.
  EXPECT_LE(response.param().request_deadline() - sent_deadline.tv_sec, 1);
  EXPECT_GE(response.param().request_deadline() - sent_deadline.tv_sec, -1);

  delete stub;
}

// Ask server to echo back the deadline it sees. The rpc has no deadline.
TEST_F(End2endTest, EchoDeadlineForNoDeadlineRpc) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());
  EXPECT_EQ(response.param().request_deadline(), gpr_inf_future.tv_sec);

  delete stub;
}

TEST_F(End2endTest, UnimplementedRpc) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub->Unimplemented(&context, request, &response);
  EXPECT_FALSE(s.IsOk());
  EXPECT_EQ(s.code(), grpc::StatusCode::UNIMPLEMENTED);
  EXPECT_EQ(s.details(), "");
  EXPECT_EQ(response.message(), "");

  delete stub;
}

TEST_F(End2endTest, RequestStreamOneRequest) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  ClientWriter<EchoRequest>* stream = stub->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Wait();
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());

  delete stream;
  delete stub;
}

TEST_F(End2endTest, RequestStreamTwoRequests) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  ClientWriter<EchoRequest>* stream = stub->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Wait();
  EXPECT_EQ(response.message(), "hellohello");
  EXPECT_TRUE(s.IsOk());

  delete stream;
  delete stub;
}

TEST_F(End2endTest, ResponseStream) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  ClientReader<EchoResponse>* stream = stub->ResponseStream(&context, &request);
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "0");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "1");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "2");
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Wait();
  EXPECT_TRUE(s.IsOk());

  delete stream;
  delete stub;
}

TEST_F(End2endTest, BidiStream) {
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), ChannelArguments());
  TestService::Stub* stub = TestService::NewStub(channel);
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  grpc::string msg("hello");

  ClientReaderWriter<EchoRequest, EchoResponse>* stream =
      stub->BidiStream(&context);

  request.set_message(msg + "0");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  request.set_message(msg + "1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  request.set_message(msg + "2");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message());

  stream->WritesDone();
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Wait();
  EXPECT_TRUE(s.IsOk());

  delete stream;
  delete stub;
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
