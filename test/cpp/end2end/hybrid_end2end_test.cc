//
//
// Copyright 2016 gRPC authors.
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

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/generic/async_generic_service.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/byte_buffer_proto_helper.h"

namespace grpc {
namespace testing {
namespace {

void* tag(int i) { return reinterpret_cast<void*>(i); }

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
void HandleEcho(Service* service, ServerCompletionQueue* cq, bool dup_service) {
  ServerContext srv_ctx;
  grpc::ServerAsyncResponseWriter<EchoResponse> response_writer(&srv_ctx);
  EchoRequest recv_request;
  EchoResponse send_response;
  service->RequestEcho(&srv_ctx, &recv_request, &response_writer, cq, cq,
                       tag(1));
  Verify(cq, 1, true);
  send_response.set_message(recv_request.message());
  if (dup_service) {
    send_response.mutable_message()->append("_dup");
  }
  response_writer.Finish(send_response, Status::OK, tag(2));
  Verify(cq, 2, true);
}

// Handlers to handle raw request at a server. To be run in a
// separate thread. Note that this is the same as the async version, except
// that the req/resp are ByteBuffers
template <class Service>
void HandleRawEcho(Service* service, ServerCompletionQueue* cq,
                   bool /*dup_service*/) {
  ServerContext srv_ctx;
  GenericServerAsyncResponseWriter response_writer(&srv_ctx);
  ByteBuffer recv_buffer;
  service->RequestEcho(&srv_ctx, &recv_buffer, &response_writer, cq, cq,
                       tag(1));
  Verify(cq, 1, true);
  EchoRequest recv_request;
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
  EchoResponse send_response;
  send_response.set_message(recv_request.message());
  auto send_buffer = SerializeToByteBuffer(&send_response);
  response_writer.Finish(*send_buffer, Status::OK, tag(2));
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
void HandleRawClientStreaming(Service* service, ServerCompletionQueue* cq) {
  ServerContext srv_ctx;
  ByteBuffer recv_buffer;
  EchoRequest recv_request;
  EchoResponse send_response;
  GenericServerAsyncReader srv_stream(&srv_ctx);
  service->RequestRequestStream(&srv_ctx, &srv_stream, cq, cq, tag(1));
  Verify(cq, 1, true);
  int i = 1;
  while (true) {
    i++;
    srv_stream.Read(&recv_buffer, tag(i));
    if (!VerifyReturnSuccess(cq, i)) {
      break;
    }
    EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
    send_response.mutable_message()->append(recv_request.message());
  }
  auto send_buffer = SerializeToByteBuffer(&send_response);
  srv_stream.Finish(*send_buffer, Status::OK, tag(100));
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

void HandleGenericEcho(GenericServerAsyncReaderWriter* stream,
                       CompletionQueue* cq) {
  ByteBuffer recv_buffer;
  stream->Read(&recv_buffer, tag(2));
  Verify(cq, 2, true);
  EchoRequest recv_request;
  EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
  EchoResponse send_response;
  send_response.set_message(recv_request.message());
  auto send_buffer = SerializeToByteBuffer(&send_response);
  stream->Write(*send_buffer, tag(3));
  Verify(cq, 3, true);
  stream->Finish(Status::OK, tag(4));
  Verify(cq, 4, true);
}

void HandleGenericRequestStream(GenericServerAsyncReaderWriter* stream,
                                CompletionQueue* cq) {
  ByteBuffer recv_buffer;
  EchoRequest recv_request;
  EchoResponse send_response;
  int i = 1;
  while (true) {
    i++;
    stream->Read(&recv_buffer, tag(i));
    if (!VerifyReturnSuccess(cq, i)) {
      break;
    }
    EXPECT_TRUE(ParseFromByteBuffer(&recv_buffer, &recv_request));
    send_response.mutable_message()->append(recv_request.message());
  }
  auto send_buffer = SerializeToByteBuffer(&send_response);
  stream->Write(*send_buffer, tag(99));
  Verify(cq, 99, true);
  stream->Finish(Status::OK, tag(100));
  Verify(cq, 100, true);
}

// Request and handle one generic call.
void HandleGenericCall(AsyncGenericService* service,
                       ServerCompletionQueue* cq) {
  GenericServerContext srv_ctx;
  GenericServerAsyncReaderWriter stream(&srv_ctx);
  service->RequestCall(&srv_ctx, &stream, cq, cq, tag(1));
  Verify(cq, 1, true);
  if (srv_ctx.method() == "/grpc.testing.EchoTestService/Echo") {
    HandleGenericEcho(&stream, cq);
  } else if (srv_ctx.method() ==
             "/grpc.testing.EchoTestService/RequestStream") {
    HandleGenericRequestStream(&stream, cq);
  } else {  // other methods not handled yet.
    gpr_log(GPR_ERROR, "method: %s", srv_ctx.method().c_str());
    GPR_ASSERT(0);
  }
}

class TestServiceImplDupPkg
    : public grpc::testing::duplicate::EchoTestService::Service {
 public:
  Status Echo(ServerContext* /*context*/, const EchoRequest* request,
              EchoResponse* response) override {
    response->set_message(request->message() + "_dup");
    return Status::OK;
  }
};

class HybridEnd2endTest : public ::testing::TestWithParam<bool> {
 protected:
  HybridEnd2endTest() {}

  static void SetUpTestSuite() {
#if TARGET_OS_IPHONE
    // Workaround Apple CFStream bug
    grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  }

  void SetUp() override {
    inproc_ = (::testing::UnitTest::GetInstance()
                   ->current_test_info()
                   ->value_param() != nullptr)
                  ? GetParam()
                  : false;
  }

  bool SetUpServer(grpc::Service* service1, grpc::Service* service2,
                   AsyncGenericService* generic_service,
                   CallbackGenericService* callback_generic_service,
                   int max_message_size = 0) {
    int port = grpc_pick_unused_port_or_die();
    server_address_ << "localhost:" << port;

    // Setup server
    ServerBuilder builder;
    builder.AddListeningPort(server_address_.str(),
                             grpc::InsecureServerCredentials());
    // Always add a sync unimplemented service: we rely on having at least one
    // synchronous method to get a listening cq
    builder.RegisterService(&unimplemented_service_);
    builder.RegisterService(service1);
    if (service2) {
      builder.RegisterService(service2);
    }
    if (generic_service) {
      builder.RegisterAsyncGenericService(generic_service);
    }
    if (callback_generic_service) {
      builder.RegisterCallbackGenericService(callback_generic_service);
    }

    if (max_message_size != 0) {
      builder.SetMaxMessageSize(max_message_size);
    }

    // Create a separate cq for each potential handler.
    for (int i = 0; i < 5; i++) {
      cqs_.push_back(builder.AddCompletionQueue(false));
    }
    server_ = builder.BuildAndStart();

    // If there is a generic callback service, this setup is only successful if
    // we have an iomgr that can run in the background or are inprocess
    return !callback_generic_service || grpc_iomgr_run_in_background() ||
           inproc_;
  }

  void TearDown() override {
    if (server_) {
      server_->Shutdown();
    }
    void* ignored_tag;
    bool ignored_ok;
    for (auto it = cqs_.begin(); it != cqs_.end(); ++it) {
      (*it)->Shutdown();
      while ((*it)->Next(&ignored_tag, &ignored_ok)) {
      }
    }
  }

  void ResetStub() {
    std::shared_ptr<Channel> channel =
        inproc_ ? server_->InProcessChannel(ChannelArguments())
                : grpc::CreateChannel(server_address_.str(),
                                      InsecureChannelCredentials());
    stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

  // Test all rpc methods.
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
    cli_ctx.set_wait_for_ready(true);
    send_request.set_message("Hello");
    Status recv_status = stub_->Echo(&cli_ctx, send_request, &recv_response);
    EXPECT_EQ(send_request.message(), recv_response.message());
    EXPECT_TRUE(recv_status.ok());
  }

  void SendEchoToDupService() {
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), InsecureChannelCredentials());
    auto stub = grpc::testing::duplicate::EchoTestService::NewStub(channel);
    EchoRequest send_request;
    EchoResponse recv_response;
    ClientContext cli_ctx;
    cli_ctx.set_wait_for_ready(true);
    send_request.set_message("Hello");
    Status recv_status = stub->Echo(&cli_ctx, send_request, &recv_response);
    EXPECT_EQ(send_request.message() + "_dup", recv_response.message());
    EXPECT_TRUE(recv_status.ok());
  }

  void SendSimpleClientStreaming() {
    EchoRequest send_request;
    EchoResponse recv_response;
    std::string expected_message;
    ClientContext cli_ctx;
    cli_ctx.set_wait_for_ready(true);
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
    context.set_wait_for_ready(true);
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

  void SendSimpleServerStreamingToDupService() {
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), InsecureChannelCredentials());
    auto stub = grpc::testing::duplicate::EchoTestService::NewStub(channel);
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    context.set_wait_for_ready(true);
    request.set_message("hello");

    auto stream = stub->ResponseStream(&context, request);
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), request.message() + "0_dup");
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), request.message() + "1_dup");
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), request.message() + "2_dup");
    EXPECT_FALSE(stream->Read(&response));

    Status s = stream->Finish();
    EXPECT_TRUE(s.ok());
  }

  void SendBidiStreaming() {
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    context.set_wait_for_ready(true);
    std::string msg("hello");

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

  grpc::testing::UnimplementedEchoService::Service unimplemented_service_;
  std::vector<std::unique_ptr<ServerCompletionQueue>> cqs_;
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
  bool inproc_;
};

TEST_F(HybridEnd2endTest, AsyncEcho) {
  typedef EchoTestService::WithAsyncMethod_Echo<TestServiceImpl> SType;
  SType service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  ResetStub();
  std::thread echo_handler_thread(HandleEcho<SType>, &service, cqs_[0].get(),
                                  false);
  TestAllMethods();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, RawEcho) {
  typedef EchoTestService::WithRawMethod_Echo<TestServiceImpl> SType;
  SType service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  ResetStub();
  std::thread echo_handler_thread(HandleRawEcho<SType>, &service, cqs_[0].get(),
                                  false);
  TestAllMethods();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, RawRequestStream) {
  typedef EchoTestService::WithRawMethod_RequestStream<TestServiceImpl> SType;
  SType service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  ResetStub();
  std::thread request_stream_handler_thread(HandleRawClientStreaming<SType>,
                                            &service, cqs_[0].get());
  TestAllMethods();
  request_stream_handler_thread.join();
}

TEST_F(HybridEnd2endTest, AsyncEchoRawRequestStream) {
  typedef EchoTestService::WithRawMethod_RequestStream<
      EchoTestService::WithAsyncMethod_Echo<TestServiceImpl>>
      SType;
  SType service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  ResetStub();
  std::thread echo_handler_thread(HandleEcho<SType>, &service, cqs_[0].get(),
                                  false);
  std::thread request_stream_handler_thread(HandleRawClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  request_stream_handler_thread.join();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, GenericEchoRawRequestStream) {
  typedef EchoTestService::WithRawMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<TestServiceImpl>>
      SType;
  SType service;
  AsyncGenericService generic_service;
  SetUpServer(&service, nullptr, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  std::thread request_stream_handler_thread(HandleRawClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  generic_handler_thread.join();
  request_stream_handler_thread.join();
}

TEST_F(HybridEnd2endTest, AsyncEchoRequestStream) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_Echo<TestServiceImpl>>
      SType;
  SType service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  ResetStub();
  std::thread echo_handler_thread(HandleEcho<SType>, &service, cqs_[0].get(),
                                  false);
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  echo_handler_thread.join();
  request_stream_handler_thread.join();
}

TEST_F(HybridEnd2endTest, AsyncRequestStreamResponseStream) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service with one sync method.
TEST_F(HybridEnd2endTest, AsyncRequestStreamResponseStreamSyncDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  TestServiceImplDupPkg dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendEchoToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service with one sync streamed unary method.
class StreamedUnaryDupPkg
    : public duplicate::EchoTestService::WithStreamedUnaryMethod_Echo<
          TestServiceImplDupPkg> {
 public:
  Status StreamedEcho(
      ServerContext* /*context*/,
      ServerUnaryStreamer<EchoRequest, EchoResponse>* stream) override {
    EchoRequest req;
    EchoResponse resp;
    uint32_t next_msg_sz;
    stream->NextMessageSize(&next_msg_sz);
    gpr_log(GPR_INFO, "Streamed Unary Next Message Size is %u", next_msg_sz);
    GPR_ASSERT(stream->Read(&req));
    resp.set_message(req.message() + "_dup");
    GPR_ASSERT(stream->Write(resp));
    return Status::OK;
  }
};

TEST_F(HybridEnd2endTest,
       AsyncRequestStreamResponseStreamSyncStreamedUnaryDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  StreamedUnaryDupPkg dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr, 8192);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendEchoToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service that is fully Streamed Unary
class FullyStreamedUnaryDupPkg
    : public duplicate::EchoTestService::StreamedUnaryService {
 public:
  Status StreamedEcho(
      ServerContext* /*context*/,
      ServerUnaryStreamer<EchoRequest, EchoResponse>* stream) override {
    EchoRequest req;
    EchoResponse resp;
    uint32_t next_msg_sz;
    stream->NextMessageSize(&next_msg_sz);
    gpr_log(GPR_INFO, "Streamed Unary Next Message Size is %u", next_msg_sz);
    GPR_ASSERT(stream->Read(&req));
    resp.set_message(req.message() + "_dup");
    GPR_ASSERT(stream->Write(resp));
    return Status::OK;
  }
};

TEST_F(HybridEnd2endTest,
       AsyncRequestStreamResponseStreamSyncFullyStreamedUnaryDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  FullyStreamedUnaryDupPkg dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr, 8192);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendEchoToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service with one sync split server streaming method.
class SplitResponseStreamDupPkg
    : public duplicate::EchoTestService::
          WithSplitStreamingMethod_ResponseStream<TestServiceImplDupPkg> {
 public:
  Status StreamedResponseStream(
      ServerContext* /*context*/,
      ServerSplitStreamer<EchoRequest, EchoResponse>* stream) override {
    EchoRequest req;
    EchoResponse resp;
    uint32_t next_msg_sz;
    stream->NextMessageSize(&next_msg_sz);
    gpr_log(GPR_INFO, "Split Streamed Next Message Size is %u", next_msg_sz);
    GPR_ASSERT(stream->Read(&req));
    for (int i = 0; i < kServerDefaultResponseStreamsToSend; i++) {
      resp.set_message(req.message() + std::to_string(i) + "_dup");
      GPR_ASSERT(stream->Write(resp));
    }
    return Status::OK;
  }
};

TEST_F(HybridEnd2endTest,
       AsyncRequestStreamResponseStreamSyncSplitStreamedDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  SplitResponseStreamDupPkg dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr, 8192);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendSimpleServerStreamingToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service that is fully split server streamed
class FullySplitStreamedDupPkg
    : public duplicate::EchoTestService::SplitStreamedService {
 public:
  Status StreamedResponseStream(
      ServerContext* /*context*/,
      ServerSplitStreamer<EchoRequest, EchoResponse>* stream) override {
    EchoRequest req;
    EchoResponse resp;
    uint32_t next_msg_sz;
    stream->NextMessageSize(&next_msg_sz);
    gpr_log(GPR_INFO, "Split Streamed Next Message Size is %u", next_msg_sz);
    GPR_ASSERT(stream->Read(&req));
    for (int i = 0; i < kServerDefaultResponseStreamsToSend; i++) {
      resp.set_message(req.message() + std::to_string(i) + "_dup");
      GPR_ASSERT(stream->Write(resp));
    }
    return Status::OK;
  }
};

TEST_F(HybridEnd2endTest,
       asyncRequestStreamResponseStreamFullySplitStreamedDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  FullySplitStreamedDupPkg dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr, 8192);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendSimpleServerStreamingToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service that is fully server streamed
class FullyStreamedDupPkg : public duplicate::EchoTestService::StreamedService {
 public:
  Status StreamedEcho(
      ServerContext* /*context*/,
      ServerUnaryStreamer<EchoRequest, EchoResponse>* stream) override {
    EchoRequest req;
    EchoResponse resp;
    uint32_t next_msg_sz;
    stream->NextMessageSize(&next_msg_sz);
    gpr_log(GPR_INFO, "Streamed Unary Next Message Size is %u", next_msg_sz);
    GPR_ASSERT(stream->Read(&req));
    resp.set_message(req.message() + "_dup");
    GPR_ASSERT(stream->Write(resp));
    return Status::OK;
  }
  Status StreamedResponseStream(
      ServerContext* /*context*/,
      ServerSplitStreamer<EchoRequest, EchoResponse>* stream) override {
    EchoRequest req;
    EchoResponse resp;
    uint32_t next_msg_sz;
    stream->NextMessageSize(&next_msg_sz);
    gpr_log(GPR_INFO, "Split Streamed Next Message Size is %u", next_msg_sz);
    GPR_ASSERT(stream->Read(&req));
    for (int i = 0; i < kServerDefaultResponseStreamsToSend; i++) {
      resp.set_message(req.message() + std::to_string(i) + "_dup");
      GPR_ASSERT(stream->Write(resp));
    }
    return Status::OK;
  }
};

TEST_F(HybridEnd2endTest,
       AsyncRequestStreamResponseStreamFullyStreamedDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  FullyStreamedDupPkg dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr, 8192);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendEchoToDupService();
  SendSimpleServerStreamingToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service with one async method.
TEST_F(HybridEnd2endTest, AsyncRequestStreamResponseStreamAsyncDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>
      SType;
  SType service;
  duplicate::EchoTestService::AsyncService dup_service;
  SetUpServer(&service, &dup_service, nullptr, nullptr);
  ResetStub();
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  std::thread echo_handler_thread(
      HandleEcho<duplicate::EchoTestService::AsyncService>, &dup_service,
      cqs_[2].get(), true);
  TestAllMethods();
  SendEchoToDupService();
  response_stream_handler_thread.join();
  request_stream_handler_thread.join();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, GenericEcho) {
  EchoTestService::WithGenericMethod_Echo<TestServiceImpl> service;
  AsyncGenericService generic_service;
  SetUpServer(&service, nullptr, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  TestAllMethods();
  generic_handler_thread.join();
}

TEST_P(HybridEnd2endTest, CallbackGenericEcho) {
  EchoTestService::WithGenericMethod_Echo<TestServiceImpl> service;
  class GenericEchoService : public CallbackGenericService {
   private:
    ServerGenericBidiReactor* CreateReactor(
        GenericCallbackServerContext* context) override {
      EXPECT_EQ(context->method(), "/grpc.testing.EchoTestService/Echo");
      gpr_log(GPR_DEBUG, "Constructor of generic service %d",
              static_cast<int>(context->deadline().time_since_epoch().count()));

      class Reactor : public ServerGenericBidiReactor {
       public:
        Reactor() { StartRead(&request_); }

       private:
        void OnDone() override { delete this; }
        void OnReadDone(bool ok) override {
          if (!ok) {
            EXPECT_EQ(reads_complete_, 1);
          } else {
            EXPECT_EQ(reads_complete_++, 0);
            response_ = request_;
            StartWrite(&response_);
            StartRead(&request_);
          }
        }
        void OnWriteDone(bool ok) override {
          Finish(ok ? Status::OK
                    : Status(StatusCode::UNKNOWN, "Unexpected failure"));
        }
        ByteBuffer request_;
        ByteBuffer response_;
        std::atomic_int reads_complete_{0};
      };
      return new Reactor;
    }
  } generic_service;

  if (!SetUpServer(&service, nullptr, nullptr, &generic_service)) {
    return;
  }
  ResetStub();
  TestAllMethods();
}

TEST_F(HybridEnd2endTest, GenericEchoAsyncRequestStream) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<TestServiceImpl>>
      SType;
  SType service;
  AsyncGenericService generic_service;
  SetUpServer(&service, nullptr, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  generic_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service with one sync method.
TEST_F(HybridEnd2endTest, GenericEchoAsyncRequestStreamSyncDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<TestServiceImpl>>
      SType;
  SType service;
  AsyncGenericService generic_service;
  TestServiceImplDupPkg dup_service;
  SetUpServer(&service, &dup_service, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  TestAllMethods();
  SendEchoToDupService();
  generic_handler_thread.join();
  request_stream_handler_thread.join();
}

// Add a second service with one async method.
TEST_F(HybridEnd2endTest, GenericEchoAsyncRequestStreamAsyncDupService) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<TestServiceImpl>>
      SType;
  SType service;
  AsyncGenericService generic_service;
  duplicate::EchoTestService::AsyncService dup_service;
  SetUpServer(&service, &dup_service, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  std::thread echo_handler_thread(
      HandleEcho<duplicate::EchoTestService::AsyncService>, &dup_service,
      cqs_[2].get(), true);
  TestAllMethods();
  SendEchoToDupService();
  generic_handler_thread.join();
  request_stream_handler_thread.join();
  echo_handler_thread.join();
}

TEST_F(HybridEnd2endTest, GenericEchoAsyncRequestStreamResponseStream) {
  typedef EchoTestService::WithAsyncMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<
          EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>>
      SType;
  SType service;
  AsyncGenericService generic_service;
  SetUpServer(&service, nullptr, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  std::thread request_stream_handler_thread(HandleClientStreaming<SType>,
                                            &service, cqs_[1].get());
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[2].get());
  TestAllMethods();
  generic_handler_thread.join();
  request_stream_handler_thread.join();
  response_stream_handler_thread.join();
}

TEST_F(HybridEnd2endTest, GenericEchoRequestStreamAsyncResponseStream) {
  typedef EchoTestService::WithGenericMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<
          EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>>
      SType;
  SType service;
  AsyncGenericService generic_service;
  SetUpServer(&service, nullptr, &generic_service, nullptr);
  ResetStub();
  std::thread generic_handler_thread(HandleGenericCall, &generic_service,
                                     cqs_[0].get());
  std::thread generic_handler_thread2(HandleGenericCall, &generic_service,
                                      cqs_[1].get());
  std::thread response_stream_handler_thread(HandleServerStreaming<SType>,
                                             &service, cqs_[2].get());
  TestAllMethods();
  generic_handler_thread.join();
  generic_handler_thread2.join();
  response_stream_handler_thread.join();
}

// If WithGenericMethod is called and no generic service is registered, the
// server will fail to build.
TEST_F(HybridEnd2endTest, GenericMethodWithoutGenericService) {
  EchoTestService::WithGenericMethod_RequestStream<
      EchoTestService::WithGenericMethod_Echo<
          EchoTestService::WithAsyncMethod_ResponseStream<TestServiceImpl>>>
      service;
  SetUpServer(&service, nullptr, nullptr, nullptr);
  EXPECT_EQ(nullptr, server_.get());
}

INSTANTIATE_TEST_SUITE_P(HybridEnd2endTest, HybridEnd2endTest,
                         ::testing::Bool());

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
