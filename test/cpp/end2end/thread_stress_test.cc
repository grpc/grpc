//
//
// Copyright 2015 gRPC authors.
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
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include <cinttypes>
#include <mutex>
#include <thread>

#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/util/env.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"

const int kNumThreads = 10;  // Number of threads
const int kNumAsyncSendThreads = 2;
const int kNumAsyncReceiveThreads = 5;
const int kNumAsyncServerThreads = 5;
const int kNumRpcs = 1000;  // Number of RPCs per thread

namespace grpc {
namespace testing {

class TestServiceImpl : public grpc::testing::EchoTestService::Service {
 public:
  TestServiceImpl() {}

  Status Echo(ServerContext* /*context*/, const EchoRequest* request,
              EchoResponse* response) override {
    response->set_message(request->message());
    return Status::OK;
  }
};

template <class Service>
class CommonStressTest {
 public:
  CommonStressTest() : kMaxMessageSize_(8192) {
#if TARGET_OS_IPHONE
    // Workaround Apple CFStream bug
    grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  }
  virtual ~CommonStressTest() {}
  virtual void SetUp() = 0;
  virtual void TearDown() = 0;
  virtual void ResetStub() = 0;
  virtual bool AllowExhaustion() = 0;
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
    std::shared_ptr<Channel> channel = grpc::CreateChannel(
        server_address_.str(), InsecureChannelCredentials());
    this->stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }
  bool AllowExhaustion() override { return false; }

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

template <class Service, bool allow_resource_exhaustion>
class CommonStressTestInproc : public CommonStressTest<Service> {
 public:
  void ResetStub() override {
    ChannelArguments args;
    std::shared_ptr<Channel> channel = this->server_->InProcessChannel(args);
    this->stub_ = grpc::testing::EchoTestService::NewStub(channel);
  }
  bool AllowExhaustion() override { return allow_resource_exhaustion; }

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
class CommonStressTestSyncServerLowThreadCount : public BaseClass {
 public:
  void SetUp() override {
    ServerBuilder builder;
    ResourceQuota quota;
    this->SetUpStart(&builder, &service_);
    quota.SetMaxThreads(4);
    builder.SetResourceQuota(quota);
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
      grpc::internal::MutexLock l(&mu_);
      this->TearDownStart();
      shutting_down_ = true;
      cq_->Shutdown();
    }

    for (int i = 0; i < kNumAsyncServerThreads; i++) {
      server_threads_[i].join();
    }

    void* ignored_tag;
    bool ignored_ok;
    while (cq_->Next(&ignored_tag, &ignored_ok)) {
    }
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
    grpc::internal::MutexLock l(&mu_);
    if (!shutting_down_) {
      contexts_[i].state = Context::READY;
      contexts_[i].srv_ctx.reset(new ServerContext);
      contexts_[i].response_writer.reset(
          new grpc::ServerAsyncResponseWriter<EchoResponse>(
              contexts_[i].srv_ctx.get()));
      service_.RequestEcho(contexts_[i].srv_ctx.get(),
                           &contexts_[i].recv_request,
                           contexts_[i].response_writer.get(), cq_.get(),
                           cq_.get(), reinterpret_cast<void*>(i));
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
  grpc::testing::EchoTestService::AsyncService service_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  bool shutting_down_;
  grpc::internal::Mutex mu_;
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

static void SendRpc(grpc::testing::EchoTestService::Stub* stub, int num_rpcs,
                    bool allow_exhaustion, gpr_atm* errors) {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    Status s = stub->Echo(&context, request, &response);
    EXPECT_TRUE(s.ok() || (allow_exhaustion &&
                           s.error_code() == StatusCode::RESOURCE_EXHAUSTED));
    if (!s.ok()) {
      if (!(allow_exhaustion &&
            s.error_code() == StatusCode::RESOURCE_EXHAUSTED)) {
        LOG(ERROR) << "RPC error: " << s.error_code() << ": "
                   << s.error_message();
      }
      gpr_atm_no_barrier_fetch_add(errors, gpr_atm{1});
    } else {
      EXPECT_EQ(response.message(), request.message());
    }
  }
}

typedef ::testing::Types<
    CommonStressTestSyncServer<CommonStressTestInsecure<TestServiceImpl>>,
    CommonStressTestAsyncServer<
        CommonStressTestInsecure<grpc::testing::EchoTestService::AsyncService>>>
    CommonTypes;
TYPED_TEST_SUITE(End2endTest, CommonTypes);
TYPED_TEST(End2endTest, ThreadStress) {
  this->common_.ResetStub();
  std::vector<std::thread> threads;
  gpr_atm errors;
  gpr_atm_rel_store(&errors, gpr_atm{0});
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back(SendRpc, this->common_.GetStub(), kNumRpcs,
                         this->common_.AllowExhaustion(), &errors);
  }
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
  }
  uint64_t error_cnt = static_cast<uint64_t>(gpr_atm_no_barrier_load(&errors));
  if (error_cnt != 0) {
    LOG(INFO) << "RPC error count: " << error_cnt;
  }
  // If this test allows resource exhaustion, expect that it actually sees some
  if (this->common_.AllowExhaustion()) {
    EXPECT_GT(error_cnt, 0);
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
    while (cq_.Next(&ignored_tag, &ignored_ok)) {
    }
    common_.TearDown();
  }

  void Wait() {
    grpc::internal::MutexLock l(&mu_);
    while (rpcs_outstanding_ != 0) {
      cv_.Wait(&mu_);
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
      request.set_message("Hello: " + std::to_string(i));
      call->response_reader =
          common_.GetStub()->AsyncEcho(&call->context, request, &cq_);
      call->response_reader->Finish(&call->response, &call->status, call);

      grpc::internal::MutexLock l(&mu_);
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
        VLOG(2) << "Error: " << call->status.error_code();
      }
      delete call;

      bool notify;
      {
        grpc::internal::MutexLock l(&mu_);
        rpcs_outstanding_--;
        notify = (rpcs_outstanding_ == 0);
      }
      if (notify) {
        cv_.Signal();
      }
    }
  }

  Common common_;
  CompletionQueue cq_;
  grpc::internal::Mutex mu_;
  grpc::internal::CondVar cv_;
  int rpcs_outstanding_;
};

TYPED_TEST_SUITE(AsyncClientEnd2endTest, CommonTypes);
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
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
