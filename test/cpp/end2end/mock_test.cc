/*
 *
 * Copyright 2015 gRPC authors.
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

#include <climits>

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/test/default_reactor_test_peer.h>

#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "src/proto/grpc/testing/echo_mock.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include <grpcpp/test/mock_stream.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>

using grpc::testing::DefaultReactorTestPeer;
using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using grpc::testing::EchoTestService;
using grpc::testing::MockClientReaderWriter;
using std::vector;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::WithArg;

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
    std::string msg("hello");
    std::string exp(msg);

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

    std::string exp = "";
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
    std::string msg("hello");

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

class CallbackTestServiceImpl
    : public EchoTestService::ExperimentalCallbackService {
 public:
  experimental::ServerUnaryReactor* Echo(
      experimental::CallbackServerContext* context, const EchoRequest* request,
      EchoResponse* response) override {
    // Make the mock service explicitly treat empty input messages as invalid
    // arguments so that we can test various results of status. In general, a
    // mocked service should just use the original service methods, but we are
    // adding this variance in Status return value just to improve coverage in
    // this test.
    auto* reactor = context->DefaultReactor();
    if (request->message().length() > 0) {
      response->set_message(request->message());
      reactor->Finish(Status::OK);
    } else {
      reactor->Finish(Status(StatusCode::INVALID_ARGUMENT, "Invalid request"));
    }
    return reactor;
  }
};

class MockCallbackTest : public ::testing::Test {
 protected:
  CallbackTestServiceImpl service_;
  ServerContext context_;
};

TEST_F(MockCallbackTest, MockedCallSucceedsWithWait) {
  experimental::CallbackServerContext ctx;
  EchoRequest req;
  EchoResponse resp;
  grpc::internal::Mutex mu;
  grpc::internal::CondVar cv;
  grpc::Status status;
  bool status_set = false;
  DefaultReactorTestPeer peer(&ctx, [&](::grpc::Status s) {
    grpc::internal::MutexLock l(&mu);
    status_set = true;
    status = std::move(s);
    cv.Signal();
  });

  req.set_message("mock 1");
  auto* reactor = service_.Echo(&ctx, &req, &resp);
  grpc::internal::WaitUntil(&cv, &mu, [&] {
    grpc::internal::MutexLock l(&mu);
    return status_set;
  });
  EXPECT_EQ(reactor, peer.reactor());
  EXPECT_TRUE(peer.test_status_set());
  EXPECT_TRUE(peer.test_status().ok());
  EXPECT_TRUE(status_set);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(req.message(), resp.message());
}

TEST_F(MockCallbackTest, MockedCallSucceeds) {
  experimental::CallbackServerContext ctx;
  EchoRequest req;
  EchoResponse resp;
  DefaultReactorTestPeer peer(&ctx);

  req.set_message("ha ha, consider yourself mocked.");
  auto* reactor = service_.Echo(&ctx, &req, &resp);
  EXPECT_EQ(reactor, peer.reactor());
  EXPECT_TRUE(peer.test_status_set());
  EXPECT_TRUE(peer.test_status().ok());
}

TEST_F(MockCallbackTest, MockedCallFails) {
  experimental::CallbackServerContext ctx;
  EchoRequest req;
  EchoResponse resp;
  DefaultReactorTestPeer peer(&ctx);

  auto* reactor = service_.Echo(&ctx, &req, &resp);
  EXPECT_EQ(reactor, peer.reactor());
  EXPECT_TRUE(peer.test_status_set());
  EXPECT_EQ(peer.test_status().error_code(), StatusCode::INVALID_ARGUMENT);
}

class TestServiceImpl : public EchoTestService::Service {
 public:
  Status Echo(ServerContext* /*context*/, const EchoRequest* request,
              EchoResponse* response) override {
    response->set_message(request->message());
    return Status::OK;
  }

  Status RequestStream(ServerContext* /*context*/,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override {
    EchoRequest request;
    std::string resp("");
    while (reader->Read(&request)) {
      gpr_log(GPR_INFO, "recv msg %s", request.message().c_str());
      resp.append(request.message());
    }
    response->set_message(resp);
    return Status::OK;
  }

  Status ResponseStream(ServerContext* /*context*/, const EchoRequest* request,
                        ServerWriter<EchoResponse>* writer) override {
    EchoResponse response;
    vector<std::string> tokens = split(request->message());
    for (const std::string& token : tokens) {
      response.set_message(token);
      writer->Write(response);
    }
    return Status::OK;
  }

  Status BidiStream(
      ServerContext* /*context*/,
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
  vector<std::string> split(const std::string& input) {
    std::string buff("");
    vector<std::string> result;

    for (auto n : input) {
      if (n != ' ') {
        buff += n;
        continue;
      }
      if (buff.empty()) continue;
      result.push_back(buff);
      buff = "";
    }
    if (!buff.empty()) result.push_back(buff);

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
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), InsecureChannelCredentials());
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
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
