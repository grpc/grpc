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
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <gtest/gtest.h>

#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/util/string_ref_helper.h"

namespace grpc {
namespace testing {

namespace {

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  TestServiceImpl() : signal_client_(false), host_() {}
  explicit TestServiceImpl(const grpc::string& host)
      : signal_client_(false), host_(new grpc::string(host)) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) GRPC_OVERRIDE {
    response->set_message(request->message());
    if (host_) {
      response->mutable_param()->set_host(*host_);
    }
    if (request->has_param() && request->param().client_cancel_after_us()) {
      {
        std::unique_lock<std::mutex> lock(mu_);
        signal_client_ = true;
      }
      while (!context->IsCancelled()) {
        gpr_sleep_until(gpr_time_add(
            gpr_now(GPR_CLOCK_REALTIME),
            gpr_time_from_micros(request->param().client_cancel_after_us(),
                                 GPR_TIMESPAN)));
      }
      return Status::CANCELLED;
    } else if (request->has_param() &&
               request->param().server_cancel_after_us()) {
      gpr_sleep_until(gpr_time_add(
          gpr_now(GPR_CLOCK_REALTIME),
          gpr_time_from_micros(request->param().server_cancel_after_us(),
                               GPR_TIMESPAN)));
      return Status::CANCELLED;
    } else {
      EXPECT_FALSE(context->IsCancelled());
    }

    if (request->has_param() && request->param().echo_metadata()) {
      const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
          context->client_metadata();
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator
               iter = client_metadata.begin();
           iter != client_metadata.end(); ++iter) {
        context->AddTrailingMetadata(ToString(iter->first),
                                     ToString(iter->second));
      }
    }
    if (request->has_param() &&
        request->param().response_message_length() > 0) {
      response->set_message(
          grpc::string(request->param().response_message_length(), '\0'));
    }
    if (request->has_param() && request->param().echo_peer()) {
      response->mutable_param()->set_peer(context->peer());
    }
    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) GRPC_OVERRIDE {
    EchoRequest request;
    response->set_message("");
    int cancel_after_reads = 0;
    while (reader->Read(&request)) {
      if (cancel_after_reads == 1) {
        gpr_log(GPR_INFO, "return cancel status");
        return Status::CANCELLED;
      } else if (cancel_after_reads > 0) {
        cancel_after_reads--;
      }
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

  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }

 private:
  bool signal_client_;
  std::mutex mu_;
  std::unique_ptr<grpc::string> host_;
};

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
  do {
    send_response.mutable_message()->append(recv_request.message());
    srv_stream.Read(&recv_request, tag(2));
  } while (VerifyReturnSuccess(cq, 2));
  srv_stream.Finish(send_response, Status::OK, tag(3));
  Verify(cq, 3, true);
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
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();
  }

  void TearDown() GRPC_OVERRIDE {
    server_->Shutdown();
    void* ignored_tag;
    bool ignored_ok;
    cq_->Shutdown();
    while (cq_->Next(&ignored_tag, &ignored_ok))
      ;
  }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  void TestAllMethods() {
    SendEcho();
    SendSimpleClientStreaming();
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

  std::unique_ptr<ServerCompletionQueue> cq_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
};

TEST_F(HybridEnd2endTest, AsyncEcho) {
  EchoTestService::WithAsyncMethod_Echo<TestServiceImpl> service;
  SetUpServer(&service);
  ResetStub();
  std::thread echo_handler_thread([this, &service] { HandleEcho(&service, cq_.get()); });
  TestAllMethods();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, AsyncEchoRequestStream) {
  EchoTestService::WithAsyncMethod_RequestStream<EchoTestService::WithAsyncMethod_Echo<TestServiceImpl> > service;
  SetUpServer(&service);
  ResetStub();
  std::thread echo_handler_thread([this, &service] { HandleEcho(&service, cq_.get()); });
  std::thread request_stream_handler_thread([this, &service] { HandleClientStreaming(&service, cq_.get()); });
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
