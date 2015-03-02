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

#include <chrono>
#include <thread>

#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.pb.h"
#include "test/cpp/util/echo.pb.h"
#include "src/cpp/util/time.h"
#include "src/cpp/server/thread_pool.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/channel_interface.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/server_credentials.h>
#include <grpc++/status.h>
#include <grpc++/stream.h>
#include "test/core/util/port.h"
#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
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

class TestServiceImpl : public ::grpc::cpp::test::util::TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
    MaybeEchoDeadline(context, request, response);
    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) GRPC_OVERRIDE {
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
                        ServerWriter<EchoResponse>* writer) GRPC_OVERRIDE {
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
                    ServerReaderWriter<EchoResponse, EchoRequest>* stream)
      GRPC_OVERRIDE {
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

class TestServiceImplDupPkg
    : public ::grpc::cpp::test::util::duplicate::TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message("no package");
    return Status::OK;
  }
};

class End2endTest : public ::testing::Test {
 protected:
  End2endTest() : thread_pool_(2) {}

  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddPort(server_address_.str(), InsecureServerCredentials());
    builder.RegisterService(&service_);
    builder.RegisterService(&dup_pkg_service_);
    builder.SetThreadPool(&thread_pool_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE { server_->Shutdown(); }

  void ResetStub() {
    std::shared_ptr<ChannelInterface> channel = CreateChannel(
        server_address_.str(), InsecureCredentials(), ChannelArguments());
    stub_ = std::move(grpc::cpp::test::util::TestService::NewStub(channel));
  }

  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
  TestServiceImplDupPkg dup_pkg_service_;
  ThreadPool thread_pool_;
};

static void SendRpc(grpc::cpp::test::util::TestService::Stub* stub,
                    int num_rpcs) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    Status s = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    EXPECT_TRUE(s.IsOk());
  }
}

TEST_F(End2endTest, SimpleRpc) {
  ResetStub();
  SendRpc(stub_.get(), 1);
}

TEST_F(End2endTest, MultipleRpcs) {
  ResetStub();
  std::vector<std::thread*> threads;
  for (int i = 0; i < 10; ++i) {
    threads.push_back(new std::thread(SendRpc, stub_.get(), 10));
  }
  for (int i = 0; i < 10; ++i) {
    threads[i]->join();
    delete threads[i];
  }
}

// Set a 10us deadline and make sure proper error is returned.
TEST_F(End2endTest, RpcDeadlineExpires) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::microseconds(10);
  context.set_absolute_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, s.code());
}

// Set a long but finite deadline.
TEST_F(End2endTest, RpcLongDeadline) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::hours(1);
  context.set_absolute_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());
}

// Ask server to echo back the deadline it sees.
TEST_F(End2endTest, EchoDeadline) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(100);
  context.set_absolute_deadline(deadline);
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());
  gpr_timespec sent_deadline;
  Timepoint2Timespec(deadline, &sent_deadline);
  // Allow 1 second error.
  EXPECT_LE(response.param().request_deadline() - sent_deadline.tv_sec, 1);
  EXPECT_GE(response.param().request_deadline() - sent_deadline.tv_sec, -1);
}

// Ask server to echo back the deadline it sees. The rpc has no deadline.
TEST_F(End2endTest, EchoDeadlineForNoDeadlineRpc) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  Status s = stub_->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());
  EXPECT_EQ(response.param().request_deadline(), gpr_inf_future.tv_sec);
}

TEST_F(End2endTest, UnimplementedRpc) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub_->Unimplemented(&context, request, &response);
  EXPECT_FALSE(s.IsOk());
  EXPECT_EQ(s.code(), grpc::StatusCode::UNIMPLEMENTED);
  EXPECT_EQ(s.details(), "");
  EXPECT_EQ(response.message(), "");
}

TEST_F(End2endTest, RequestStreamOneRequest) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, RequestStreamTwoRequests) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  auto stream = stub_->RequestStream(&context, &response);
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  EXPECT_EQ(response.message(), "hellohello");
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, ResponseStream) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub_->ResponseStream(&context, request);
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "0");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "1");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), request.message() + "2");
  EXPECT_FALSE(stream->Read(&response));

  Status s = stream->Finish();
  EXPECT_TRUE(s.IsOk());
}

TEST_F(End2endTest, BidiStream) {
  ResetStub();
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  grpc::string msg("hello");

  auto stream = stub_->BidiStream(&context);

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

  Status s = stream->Finish();
  EXPECT_TRUE(s.IsOk());
}

// Talk to the two services with the same name but different package names.
// The two stubs are created on the same channel.
TEST_F(End2endTest, DiffPackageServices) {
  std::shared_ptr<ChannelInterface> channel = CreateChannel(
      server_address_.str(), InsecureCredentials(), ChannelArguments());

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub(
      grpc::cpp::test::util::TestService::NewStub(channel));
  ClientContext context;
  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ(response.message(), request.message());
  EXPECT_TRUE(s.IsOk());

  std::unique_ptr<grpc::cpp::test::util::duplicate::TestService::Stub>
      dup_pkg_stub(
          grpc::cpp::test::util::duplicate::TestService::NewStub(channel));
  ClientContext context2;
  s = dup_pkg_stub->Echo(&context2, request, &response);
  EXPECT_EQ("no package", response.message());
  EXPECT_TRUE(s.IsOk());
}

// rpc and stream should fail on bad credentials.
TEST_F(End2endTest, BadCredentials) {
  std::unique_ptr<Credentials> bad_creds =
      ServiceAccountCredentials("", "", std::chrono::seconds(1));
  EXPECT_EQ(nullptr, bad_creds.get());
  std::shared_ptr<ChannelInterface> channel =
      CreateChannel(server_address_.str(), bad_creds, ChannelArguments());
  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub(
      grpc::cpp::test::util::TestService::NewStub(channel));
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  grpc::string msg("hello");

  Status s = stub->Echo(&context, request, &response);
  EXPECT_EQ("", response.message());
  EXPECT_FALSE(s.IsOk());
  EXPECT_EQ(StatusCode::UNKNOWN, s.code());
  EXPECT_EQ("Rpc sent on a lame channel.", s.details());

  ClientContext context2;
  auto stream = stub->BidiStream(&context2);
  s = stream->Finish();
  EXPECT_FALSE(s.IsOk());
  EXPECT_EQ(StatusCode::UNKNOWN, s.code());
  EXPECT_EQ("Rpc sent on a lame channel.", s.details());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  google::protobuf::ShutdownProtobufLibrary();
  return result;
}
