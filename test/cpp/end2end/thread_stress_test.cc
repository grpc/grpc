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

#include <mutex>
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

#include "src/core/lib/surface/api_trace.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include <gtest/gtest.h>

using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using std::chrono::system_clock;

const int kNumThreads = 100;  // Number of threads
const int kNumAsyncSendThreads = 2;
const int kNumAsyncReceiveThreads = 50;
const int kNumAsyncServerThreads = 50;
const int kNumRpcs = 1000;  // Number of RPCs per thread

namespace grpc {
namespace testing {

class TestServiceImpl : public ::grpc::testing::EchoTestService::Service {
 public:
  TestServiceImpl() : signal_client_(false) {}

  Status Echo(ServerContext* context, const EchoRequest* request,
              EchoResponse* response) override {
    response->set_message(request->message());
    return Status::OK;
  }

  // Unimplemented is left unimplemented to test the returned error.

  Status RequestStream(ServerContext* context,
                       ServerReader<EchoRequest>* reader,
                       EchoResponse* response) override {
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
                        ServerWriter<EchoResponse>* writer) override {
    EchoResponse response;
    response.set_message(request->message() + "0");
    writer->Write(response);
    response.set_message(request->message() + "1");
    writer->Write(response);
    response.set_message(request->message() + "2");
    writer->Write(response);

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

  bool signal_client() {
    std::unique_lock<std::mutex> lock(mu_);
    return signal_client_;
  }

 private:
  bool signal_client_;
  std::mutex mu_;
};

template <class Service>
class CommonStressTest {
 public:
  CommonStressTest() : kMaxMessageSize_(8192) {}
  virtual ~CommonStressTest() {}
  virtual void SetUp() = 0;
  virtual void TearDown() = 0;
  virtual void ResetStub() = 0;
  grpc::testing::EchoTestService::Stub* GetStub() { return stub_.get(); }

 protected:
  std::unique_ptr<grpc::testing::EchoTestService::Stub> stub_;
  std::unique_ptr<Server> server_;

  virtual void SetUpStart(ServerBuilder* builder, Service* service) = 0;
  void SetUpStartCommon(ServerBuilder* builder, Service* service) {
    builder->RegisterService(service);
    builder->SetMaxMessageSize(
        kMaxMessageSize_);  // For testing max message size.
  }
  void SetUpEnd(ServerBuilder* builder) { server_ = builder->BuildAndStart(); }
  void TearDownStart() { server_->Shutdown(); }
  void TearDownEnd() {}

 private:
  const int kMaxMessageSize_;
};

template <class Service>
class CommonStressTestInsecure : public CommonStressTest<Service> {
 public:
  void ResetStub() override {
    std::shared_ptr<Channel> channel =
        CreateChannel(server_address_.str(), InsecureChannelCredentials());
    this->stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

 protected:
  void SetUpStart(ServerBuilder* builder, Service* service) override {
    int port = grpc_pick_unused_port_or_die();
    this->server_address_ << "localhost:" << port;
    // Setup server
    builder->AddListeningPort(server_address_.str(),
                              InsecureServerCredentials());
    this->SetUpStartCommon(builder, service);
  }

 private:
  std::ostringstream server_address_;
};

template <class Service>
class CommonStressTestInproc : public CommonStressTest<Service> {
 public:
  void ResetStub() override {
    ChannelArguments args;
    std::shared_ptr<Channel> channel = this->server_->InProcessChannel(args);
    this->stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }

 protected:
  void SetUpStart(ServerBuilder* builder, Service* service) override {
    this->SetUpStartCommon(builder, service);
  }
};

template <class BaseClass>
class CommonStressTestSyncServer : public BaseClass {
 public:
  void SetUp() override {
    ServerBuilder builder;
    this->SetUpStart(&builder, &service_);
    this->SetUpEnd(&builder);
  }
  void TearDown() override {
    this->TearDownStart();
    this->TearDownEnd();
  }

 private:
  TestServiceImpl service_;
};

template <class BaseClass>
class CommonStressTestAsyncServer : public BaseClass {
 public:
  CommonStressTestAsyncServer() : contexts_(kNumAsyncServerThreads * 100) {}
  void SetUp() override {
    shutting_down_ = false;
    ServerBuilder builder;
    this->SetUpStart(&builder, &service_);
    cq_ = builder.AddCompletionQueue();
    this->SetUpEnd(&builder);
    for (int i = 0; i < kNumAsyncServerThreads * 100; i++) {
      RefreshContext(i);
    }
    for (int i = 0; i < kNumAsyncServerThreads; i++) {
      server_threads_.emplace_back(&CommonStressTestAsyncServer::ProcessRpcs,
                                   this);
    }
  }
  void TearDown() override {
    {
      std::unique_lock<std::mutex> l(mu_);
      this->TearDownStart();
      shutting_down_ = true;
      cq_->Shutdown();
    }

    for (int i = 0; i < kNumAsyncServerThreads; i++) {
      server_threads_[i].join();
    }

    void* ignored_tag;
    bool ignored_ok;
    while (cq_->Next(&ignored_tag, &ignored_ok))
      ;
    this->TearDownEnd();
  }

 private:
  void ProcessRpcs() {
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok)) {
      if (ok) {
        int i = static_cast<int>(reinterpret_cast<intptr_t>(tag));
        switch (contexts_[i].state) {
          case Context::READY: {
            contexts_[i].state = Context::DONE;
            EchoResponse send_response;
            send_response.set_message(contexts_[i].recv_request.message());
            contexts_[i].response_writer->Finish(send_response, Status::OK,
                                                 tag);
            break;
          }
          case Context::DONE:
            RefreshContext(i);
            break;
        }
      }
    }
  }
  void RefreshContext(int i) {
    std::unique_lock<std::mutex> l(mu_);
    if (!shutting_down_) {
      contexts_[i].state = Context::READY;
      contexts_[i].srv_ctx.reset(new ServerContext);
      contexts_[i].response_writer.reset(
          new grpc::ServerAsyncResponseWriter<EchoResponse>(
              contexts_[i].srv_ctx.get()));
      service_.RequestEcho(contexts_[i].srv_ctx.get(),
                           &contexts_[i].recv_request,
                           contexts_[i].response_writer.get(), cq_.get(),
                           cq_.get(), (void*)(intptr_t)i);
    }
  }
  struct Context {
    std::unique_ptr<ServerContext> srv_ctx;
    std::unique_ptr<grpc::ServerAsyncResponseWriter<EchoResponse>>
        response_writer;
    EchoRequest recv_request;
    enum { READY, DONE } state;
  };
  std::vector<Context> contexts_;
  ::grpc::testing::EchoTestService::AsyncService service_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  bool shutting_down_;
  std::mutex mu_;
  std::vector<std::thread> server_threads_;
};

template <class Common>
class End2endTest : public ::testing::Test {
 protected:
  End2endTest() {}
  void SetUp() override { common_.SetUp(); }
  void TearDown() override { common_.TearDown(); }
  void ResetStub() { common_.ResetStub(); }

  Common common_;
};

static void SendRpc(grpc::testing::EchoTestService::Stub* stub, int num_rpcs) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    Status s = stub->Echo(&context, request, &response);
    EXPECT_EQ(response.message(), request.message());
    if (!s.ok()) {
      gpr_log(GPR_ERROR, "RPC error: %d: %s", s.error_code(),
              s.error_message().c_str());
    }
    ASSERT_TRUE(s.ok());
  }
}

typedef ::testing::Types<
    CommonStressTestSyncServer<CommonStressTestInsecure<TestServiceImpl>>,
    CommonStressTestSyncServer<CommonStressTestInproc<TestServiceImpl>>,
    CommonStressTestAsyncServer<
        CommonStressTestInsecure<grpc::testing::EchoTestService::AsyncService>>,
    CommonStressTestAsyncServer<
        CommonStressTestInproc<grpc::testing::EchoTestService::AsyncService>>>
    CommonTypes;
TYPED_TEST_CASE(End2endTest, CommonTypes);
TYPED_TEST(End2endTest, ThreadStress) {
  this->common_.ResetStub();
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(SendRpc, this->common_.GetStub(), kNumRpcs);
  }
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
  }
}

template <class Common>
class AsyncClientEnd2endTest : public ::testing::Test {
 protected:
  AsyncClientEnd2endTest() : rpcs_outstanding_(0) {}

  void SetUp() override { common_.SetUp(); }
  void TearDown() override {
    void* ignored_tag;
    bool ignored_ok;
    while (cq_.Next(&ignored_tag, &ignored_ok))
      ;
    common_.TearDown();
  }

  void Wait() {
    std::unique_lock<std::mutex> l(mu_);
    while (rpcs_outstanding_ != 0) {
      cv_.wait(l);
    }

    cq_.Shutdown();
  }

  struct AsyncClientCall {
    EchoResponse response;
    ClientContext context;
    Status status;
    std::unique_ptr<ClientAsyncResponseReader<EchoResponse>> response_reader;
  };

  void AsyncSendRpc(int num_rpcs) {
    for (int i = 0; i < num_rpcs; ++i) {
      AsyncClientCall* call = new AsyncClientCall;
      EchoRequest request;
      request.set_message("Hello: " + grpc::to_string(i));
      call->response_reader =
          common_.GetStub()->AsyncEcho(&call->context, request, &cq_);
      call->response_reader->Finish(&call->response, &call->status,
                                    (void*)call);

      std::unique_lock<std::mutex> l(mu_);
      rpcs_outstanding_++;
    }
  }

  void AsyncCompleteRpc() {
    while (true) {
      void* got_tag;
      bool ok = false;
      if (!cq_.Next(&got_tag, &ok)) break;
      AsyncClientCall* call = static_cast<AsyncClientCall*>(got_tag);
      if (!ok) {
        gpr_log(GPR_DEBUG, "Error: %d", call->status.error_code());
      }
      delete call;

      bool notify;
      {
        std::unique_lock<std::mutex> l(mu_);
        rpcs_outstanding_--;
        notify = (rpcs_outstanding_ == 0);
      }
      if (notify) {
        cv_.notify_all();
      }
    }
  }

  Common common_;
  CompletionQueue cq_;
  std::mutex mu_;
  std::condition_variable cv_;
  int rpcs_outstanding_;
};

TYPED_TEST_CASE(AsyncClientEnd2endTest, CommonTypes);
TYPED_TEST(AsyncClientEnd2endTest, ThreadStress) {
  this->common_.ResetStub();
  std::vector<std::thread> send_threads, completion_threads;
  for (int i = 0; i < kNumAsyncReceiveThreads; ++i) {
    completion_threads.emplace_back(
        &AsyncClientEnd2endTest_ThreadStress_Test<TypeParam>::AsyncCompleteRpc,
        this);
  }
  for (int i = 0; i < kNumAsyncSendThreads; ++i) {
    send_threads.emplace_back(
        &AsyncClientEnd2endTest_ThreadStress_Test<TypeParam>::AsyncSendRpc,
        this, kNumRpcs);
  }
  for (int i = 0; i < kNumAsyncSendThreads; ++i) {
    send_threads[i].join();
  }

  this->Wait();
  for (int i = 0; i < kNumAsyncReceiveThreads; ++i) {
    completion_threads[i].join();
  }
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
