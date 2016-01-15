/*
 *
 * Copyright 2016, Google Inc.
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

#include <memory>
#include <thread>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
// #include "test/cpp/util/string_ref_helper.h"

namespace grpc {
namespace testing {

namespace {

void* tag(int i) { return (void*)(intptr_t)i; }

bool VerifyReturnSuccess(CompletionQueue* cq, int i) {
  void* got_tag;
  bool ok;
  EXPECT_TRUE(cq->Next(&got_tag, &ok));
  EXPECT_EQ(tag(i), got_tag);
  return ok;
}

void Verify(CompletionQueue* cq, int i, bool expect_ok) {
  EXPECT_EQ(expect_ok, VerifyReturnSuccess(cq, i));
}

// Handlers to handle async request at a server. To be run in a separate thread.
template <class Service>
void HandleEcho(Service* service, ServerCompletionQueue* cq) {
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);
  EchoRequest recv_request;
  EchoResponse send_response;
  service->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq, cq, tag(1));
  Verify(cq, 1, true);
  send_response.set_message(recv_request.message());
  response_writer.Finish(send_response, Status::OK, tag(2));
  Verify(cq, 2, true);
}

template <class Service>
void HandleClientStreaming(Service* service, ServerCompletionQueue* cq) {
  ServerContext srv_ctx;
  EchoRequest recv_request;
  EchoResponse send_response;
  ServerAsyncReader<EchoResponse, EchoRequest> srv_stream(&srv_ctx);
  service->RequestRequestStream(&srv_ctx, &srv_stream, cq, cq, tag(1));
  Verify(cq, 1, true);
  int i = 1;
  do {
    i++;
    send_response.mutable_message()->append(recv_request.message());
    srv_stream.Read(&recv_request, tag(i));
  } while (VerifyReturnSuccess(cq, i));
  srv_stream.Finish(send_response, Status::OK, tag(100));
  Verify(cq, 100, true);
}

template <class Service>
void HandleServerStreaming(Service* service, ServerCompletionQueue* cq) {
  ServerContext srv_ctx;
  EchoRequest recv_request;
  EchoResponse send_response;
  ServerAsyncWriter<EchoResponse> srv_stream(&srv_ctx);
  service->RequestResponseStream(&srv_ctx, &recv_request, &srv_stream, cq, cq,
                                 tag(1));
  Verify(cq, 1, true);
  send_response.set_message(recv_request.message() + "0");
  srv_stream.Write(send_response, tag(2));
  Verify(cq, 2, true);
  send_response.set_message(recv_request.message() + "1");
  srv_stream.Write(send_response, tag(3));
  Verify(cq, 3, true);
  send_response.set_message(recv_request.message() + "2");
  srv_stream.Write(send_response, tag(4));
  Verify(cq, 4, true);
  srv_stream.Finish(Status::OK, tag(5));
  Verify(cq, 5, true);
}

class HybridEnd2endTest : public ::testing::Test {
 protected:
  HybridEnd2endTest() {}

  void SetUpServer(::grpc::Service* service) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;

    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(service);
    // Create a separate cq for each potential handler.
    for (int i = 0; i < 5; i++) {
      cqs_.push_back(std::move(builder.AddCompletionQueue()));
    }
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE {
    server_->Shutdown();
    void* ignored_tag;
    bool ignored_ok;
    for (auto it = cqs_.begin(); it != cqs_.end(); ++it) {
      (*it)->Shutdown();
      while ((*it)->Next(&ignored_tag, &ignored_ok))
        ;
    }
  }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  void TestAllMethods() {
    SendEcho();
    SendSimpleClientStreaming();
    SendSimpleServerStreaming();
    SendBidiStreaming();
  }

  void SendEcho() {
    EchoRequest send_request;
    EchoResponse recv_response;
    ClientContext cli_ctx;
    send_request.set_message("Hello");
    Status recv_status = stub_->Echo(&cli_ctx, send_request, &recv_response);
    EXPECT_EQ(send_request.message(), recv_response.message());
    EXPECT_TRUE(recv_status.ok());
  }

  void SendSimpleClientStreaming() {
    EchoRequest send_request;
    EchoResponse recv_response;
    grpc::string expected_message;
    ClientContext cli_ctx;
    send_request.set_message("Hello");
    auto stream = stub_->RequestStream(&cli_ctx, &recv_response);
    for (int i = 0; i < 5; i++) {
      EXPECT_TRUE(stream->Write(send_request));
      expected_message.append(send_request.message());
    }
    stream->WritesDone();
    Status recv_status = stream->Finish();
    EXPECT_EQ(expected_message, recv_response.message());
    EXPECT_TRUE(recv_status.ok());
  }

  void SendSimpleServerStreaming() {
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
    EXPECT_TRUE(s.ok());
  }

  void SendBidiStreaming() {
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
    EXPECT_FALSE(stream->Read(&response));

    Status s = stream->Finish();
    EXPECT_TRUE(s.ok());
  }

  std::vector<std::unique_ptr<ServerCompletionQueue> > cqs_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
};

TEST_F(HybridEnd2endTest, AsyncEcho) {
  EchoTestService::WithAsyncMethod_Echo<TestServiceImpl> service;
  SetUpServer(&service);
  ResetStub();
  std::thread echo_handler_thread(
      [this, &service] { HandleEcho(&service, cqs_[0].get()); });
  TestAllMethods();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, AsyncEchoRequestStream) {
  EchoTestService::WithAsyncMethod_RequestStream<EchoTestService::WithAsyncMethod_Echo<TestServiceImpl> > service;
  SetUpServer(&service);
  ResetStub();
  std::thread echo_handler_thread(
      [this, &service] { HandleEcho(&service, cqs_[0].get()); });
  std::thread request_stream_handler_thread(
      [this, &service] { HandleClientStreaming(&service, cqs_[1].get()); });
  TestAllMethods();
  echo_handler_thread.join();
  request_stream_handler_thread.join();
}

TEST_F(HybridEnd2endTest, AsyncRequestStreamResponseStream) {
  EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl> >
      service;
  SetUpServer(&service);
  ResetStub();
  std::thread echo_handler_thread(
      [this, &service] { HandleServerStreaming(&service, cqs_[0].get()); });
  std::thread request_stream_handler_thread(
      [this, &service] { HandleClientStreaming(&service, cqs_[1].get()); });
  TestAllMethods();
  echo_handler_thread.join();
  request_stream_handler_thread.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
