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

#include <climits>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_mock.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include <grpc++/test/mock_stream.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>

using namespace std;
using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using grpc::testing::EchoTestService;
using grpc::testing::MockClientReaderWriter;
using std::chrono::system_clock;
using ::testing::AtLeast;
using ::testing::SetArgPointee;
using ::testing::SaveArg;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::WithArg;
using ::testing::DoAll;

namespace grpc {
namespace testing {

namespace {
class FakeClient {
 public:
  explicit FakeClient(EchoTestService::StubInterface* stub) : stub_(stub) {}

  void DoEcho() {
    ClientContext context;
    EchoRequest request;
    EchoResponse response;
    request.set_message("hello world");
    Status s = stub_->Echo(&context, request, &response);
    EXPECT_EQ(request.message(), response.message());
    EXPECT_TRUE(s.ok());
  }

  void DoRequestStream() {
    EchoRequest request;
    EchoResponse response;

    ClientContext context;
    grpc::string msg("hello");
    grpc::string exp(msg);

    std::unique_ptr<ClientWriterInterface<EchoRequest>> cstream =
        stub_->RequestStream(&context, &response);

    request.set_message(msg);
    EXPECT_TRUE(cstream->Write(request));

    msg = ", world";
    request.set_message(msg);
    exp.append(msg);
    EXPECT_TRUE(cstream->Write(request));

    cstream->WritesDone();
    Status s = cstream->Finish();

    EXPECT_EQ(exp, response.message());
    EXPECT_TRUE(s.ok());
  }

  void DoResponseStream() {
    EchoRequest request;
    EchoResponse response;
    request.set_message("hello world");

    ClientContext context;
    std::unique_ptr<ClientReaderInterface<EchoResponse>> cstream =
        stub_->ResponseStream(&context, request);

    grpc::string exp = "";
    EXPECT_TRUE(cstream->Read(&response));
    exp.append(response.message() + " ");

    EXPECT_TRUE(cstream->Read(&response));
    exp.append(response.message());

    EXPECT_FALSE(cstream->Read(&response));
    EXPECT_EQ(request.message(), exp);

    Status s = cstream->Finish();
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

  void ResetStub(EchoTestService::StubInterface* stub) { stub_ = stub; }

 private:
  EchoTestService::StubInterface* stub_;
};

class TestServiceImpl : public EchoTestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    response->set_message(request->message());
    return Status::OK;
  }

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override {
    EchoRequest request;
    grpc::string resp("");
    while (reader->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      resp.append(request.message());
    }
    response->set_message(resp);
    return Status::OK;
  }

  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) override {
    EchoResponse response;
    vector<grpc::string> tokens = split(request->message());
    for (grpc::string token : tokens) {
      response.set_message(token);
      writer->Write(response);
    }
    return Status::OK;
  }

  Status BidiStream(
      ServerContext* context,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request;
    EchoResponse response;
    while (stream->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      response.set_message(request.message());
      stream->Write(response);
    }
    return Status::OK;
  }

 private:
  const vector<grpc::string> split(const grpc::string& input) {
    grpc::string buff("");
    vector<grpc::string> result;

    for (auto n : input) {
      if (n != ' ') {
        buff += n;
        continue;
      }
      if (buff == "") continue;
      result.push_back(buff);
      buff = "";
    }
    if (buff != "") result.push_back(buff);

    return result;
  }
};

class MockTest : public ::testing::Test {
 protected:
  MockTest() {}

  void SetUp() override {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;
    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             InsecureServerCredentials());
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
  }

  void TearDown() override { server_->Shutdown(); }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  TestServiceImpl service_;
};

// Do one real rpc and one mocked one
TEST_F(MockTest, SimpleRpc) {
  ResetStub();
  FakeClient client(stub_.get());
  client.DoEcho();
  MockEchoTestServiceStub stub;
  EchoResponse resp;
  resp.set_message("hello world");
  EXPECT_CALL(stub, Echo(_, _, _))
      .Times(AtLeast(1))
      .WillOnce(DoAll(SetArgPointee<2>(resp), Return(Status::OK)));
  client.ResetStub(&stub);
  client.DoEcho();
}

TEST_F(MockTest, ClientStream) {
  ResetStub();
  FakeClient client(stub_.get());
  client.DoRequestStream();

  MockEchoTestServiceStub stub;
  auto w = new MockClientWriter<EchoRequest>();
  EchoResponse resp;
  resp.set_message("hello, world");

  EXPECT_CALL(*w, Write(_, _)).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(*w, WritesDone());
  EXPECT_CALL(*w, Finish()).WillOnce(Return(Status::OK));

  EXPECT_CALL(stub, RequestStreamRaw(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(resp), Return(w)));
  client.ResetStub(&stub);
  client.DoRequestStream();
}

TEST_F(MockTest, ServerStream) {
  ResetStub();
  FakeClient client(stub_.get());
  client.DoResponseStream();

  MockEchoTestServiceStub stub;
  auto r = new MockClientReader<EchoResponse>();
  EchoResponse resp1;
  resp1.set_message("hello");
  EchoResponse resp2;
  resp2.set_message("world");

  EXPECT_CALL(*r, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(resp1), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(resp2), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*r, Finish()).WillOnce(Return(Status::OK));

  EXPECT_CALL(stub, ResponseStreamRaw(_, _)).WillOnce(Return(r));

  client.ResetStub(&stub);
  client.DoResponseStream();
}

ACTION_P(copy, msg) { arg0->set_message(msg->message()); }

TEST_F(MockTest, BidiStream) {
  ResetStub();
  FakeClient client(stub_.get());
  client.DoBidiStream();
  MockEchoTestServiceStub stub;
  auto rw = new MockClientReaderWriter<EchoRequest, EchoResponse>();
  EchoRequest msg;

  EXPECT_CALL(*rw, Write(_, _))
      .Times(3)
      .WillRepeatedly(DoAll(SaveArg<0>(&msg), Return(true)));
  EXPECT_CALL(*rw, Read(_))
      .WillOnce(DoAll(WithArg<0>(copy(&msg)), Return(true)))
      .WillOnce(DoAll(WithArg<0>(copy(&msg)), Return(true)))
      .WillOnce(DoAll(WithArg<0>(copy(&msg)), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*rw, WritesDone());
  EXPECT_CALL(*rw, Finish()).WillOnce(Return(Status::OK));

  EXPECT_CALL(stub, BidiStreamRaw(_)).WillOnce(Return(rw));
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
