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

#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <gtest/gtest.h>

#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/echo_duplicate.grpc.pb.h"
#include "test/cpp/util/echo.grpc.pb.h"

using grpc::cpp::test::util::EchoRequest;
using grpc::cpp::test::util::EchoResponse;
using grpc::cpp::test::util::TestService;
using std::chrono::system_clock;

namespace grpc {
namespace testing {

namespace {
template <class W, class R>
class MockClientReaderWriter GRPC_FINAL
    : public ClientReaderWriterInterface<W, R> {
 public:
  void WaitForInitialMetadata() GRPC_OVERRIDE {}
  bool Read(R* msg) GRPC_OVERRIDE { return true; }
  bool Write(const W& msg) GRPC_OVERRIDE { return true; }
  bool WritesDone() GRPC_OVERRIDE { return true; }
  Status Finish() GRPC_OVERRIDE { return Status::OK; }
};
template <>
class MockClientReaderWriter<EchoRequest, EchoResponse> GRPC_FINAL
    : public ClientReaderWriterInterface<EchoRequest, EchoResponse> {
 public:
  MockClientReaderWriter() : writes_done_(false) {}
  void WaitForInitialMetadata() GRPC_OVERRIDE {}
  bool Read(EchoResponse* msg) GRPC_OVERRIDE {
    if (writes_done_) return false;
    msg->set_message(last_message_);
    return true;
  }

  bool Write(const EchoRequest& msg,
             const WriteOptions& options) GRPC_OVERRIDE {
    gpr_log(GPR_INFO, "mock recv msg %s", msg.message().c_str());
    last_message_ = msg.message();
    return true;
  }
  bool WritesDone() GRPC_OVERRIDE {
    writes_done_ = true;
    return true;
  }
  Status Finish() GRPC_OVERRIDE { return Status::OK; }

 private:
  bool writes_done_;
  grpc::string last_message_;
};

// Mocked stub.
class MockStub : public TestService::StubInterface {
 public:
  MockStub() {}
  ~MockStub() {}
  Status Echo(ClientContext* context, const EchoRequest& request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request.message());
    return Status::OK;
  }
  Status Unimplemented(ClientContext* context, const EchoRequest& request,
                       EchoResponse* response) GRPC_OVERRIDE {
    return Status::OK;
  }

 private:
  ClientAsyncResponseReaderInterface<EchoResponse>* AsyncEchoRaw(
      ClientContext* context, const EchoRequest& request,
      CompletionQueue* cq) GRPC_OVERRIDE {
    return nullptr;
  }
  ClientWriterInterface<EchoRequest>* RequestStreamRaw(
      ClientContext* context, EchoResponse* response) GRPC_OVERRIDE {
    return nullptr;
  }
  ClientAsyncWriterInterface<EchoRequest>* AsyncRequestStreamRaw(
      ClientContext* context, EchoResponse* response, CompletionQueue* cq,
      void* tag) GRPC_OVERRIDE {
    return nullptr;
  }
  ClientReaderInterface<EchoResponse>* ResponseStreamRaw(
      ClientContext* context, const EchoRequest& request) GRPC_OVERRIDE {
    return nullptr;
  }
  ClientAsyncReaderInterface<EchoResponse>* AsyncResponseStreamRaw(
      ClientContext* context, const EchoRequest& request, CompletionQueue* cq,
      void* tag) GRPC_OVERRIDE {
    return nullptr;
  }
  ClientReaderWriterInterface<EchoRequest, EchoResponse>* BidiStreamRaw(
      ClientContext* context) GRPC_OVERRIDE {
    return new MockClientReaderWriter<EchoRequest, EchoResponse>();
  }
  ClientAsyncReaderWriterInterface<EchoRequest, EchoResponse>*
  AsyncBidiStreamRaw(ClientContext* context, CompletionQueue* cq,
                     void* tag) GRPC_OVERRIDE {
    return nullptr;
  }
  ClientAsyncResponseReaderInterface<EchoResponse>* AsyncUnimplementedRaw(
      ClientContext* context, const EchoRequest& request,
      CompletionQueue* cq) GRPC_OVERRIDE {
    return nullptr;
  }
};

class FakeClient {
 public:
  explicit FakeClient(TestService::StubInterface* stub) : stub_(stub) {}

  void DoEcho() {
    ClientContext context;
    EchoRequest request;
    EchoResponse response;
    request.set_message("hello world");
    Status s = stub_->Echo(&context, request, &response);
    EXPECT_EQ(request.message(), response.message());
    EXPECT_TRUE(s.ok());
  }

  void DoBidiStream() {
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    grpc::string msg("hello");

    std::unique_ptr<ClientReaderWriterInterface<EchoRequest, EchoResponse>>
        stream = stub_->BidiStream(&context);

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
    EXPECT_TRUE(s.ok());
  }

  void ResetStub(TestService::StubInterface* stub) { stub_ = stub; }

 private:
  TestService::StubInterface* stub_;
};

class TestServiceImpl : public TestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
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

class MockTest : public ::testing::Test {
 protected:
  MockTest() {}

  void SetUp() GRPC_OVERRIDE {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE { server_->Shutdown(); }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::cpp::test::util::TestService::NewStub(channel);
  }

  std::unique_ptr<grpc::cpp::test::util::TestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
};

// Do one real rpc and one mocked one
TEST_F(MockTest, SimpleRpc) {
  ResetStub();
  FakeClient client(stub_.get());
  client.DoEcho();
  MockStub stub;
  client.ResetStub(&stub);
  client.DoEcho();
}

TEST_F(MockTest, BidiStream) {
  ResetStub();
  FakeClient client(stub_.get());
  client.DoBidiStream();
  MockStub stub;
  client.ResetStub(&stub);
  client.DoBidiStream();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
