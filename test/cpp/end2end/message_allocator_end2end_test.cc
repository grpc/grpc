//
//
// Copyright 2019 gRPC authors.
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

#include <google/protobuf/arena.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/message_allocator.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {
namespace {

class CallbackTestServiceImpl : public EchoTestService::CallbackService {
 public:
  explicit CallbackTestServiceImpl() {}

  void SetAllocatorMutator(
      std::function<void(RpcAllocatorState* allocator_state,
                         const EchoRequest* req, EchoResponse* resp)>
          mutator) {
    allocator_mutator_ = std::move(mutator);
  }

  ServerUnaryReactor* Echo(CallbackServerContext* context,
                           const EchoRequest* request,
                           EchoResponse* response) override {
    response->set_message(request->message());
    if (allocator_mutator_) {
      allocator_mutator_(context->GetRpcAllocatorState(), request, response);
    }
    auto* reactor = context->DefaultReactor();
    reactor->Finish(Status::OK);
    return reactor;
  }

 private:
  std::function<void(RpcAllocatorState* allocator_state, const EchoRequest* req,
                     EchoResponse* resp)>
      allocator_mutator_;
};

enum class Protocol { INPROC, TCP };

class TestScenario {
 public:
  TestScenario(Protocol protocol, const std::string& creds_type)
      : protocol(protocol), credentials_type(creds_type) {}
  void Log() const;
  Protocol protocol;
  const std::string credentials_type;
};

std::ostream& operator<<(std::ostream& out, const TestScenario& scenario) {
  return out << "TestScenario{protocol="
             << (scenario.protocol == Protocol::INPROC ? "INPROC" : "TCP")
             << "," << scenario.credentials_type << "}";
}

void TestScenario::Log() const {
  std::ostringstream out;
  out << *this;
  LOG(INFO) << out.str();
}

class MessageAllocatorEnd2endTestBase
    : public ::testing::TestWithParam<TestScenario> {
 protected:
  MessageAllocatorEnd2endTestBase() { GetParam().Log(); }

  ~MessageAllocatorEnd2endTestBase() override = default;

  void CreateServer(MessageAllocator<EchoRequest, EchoResponse>* allocator) {
    ServerBuilder builder;

    auto server_creds = GetCredentialsProvider()->GetServerCredentials(
        GetParam().credentials_type);
    if (GetParam().protocol == Protocol::TCP) {
      picked_port_ = grpc_pick_unused_port_or_die();
      server_address_ << "localhost:" << picked_port_;
      builder.AddListeningPort(server_address_.str(), server_creds);
    }
    callback_service_.SetMessageAllocatorFor_Echo(allocator);
    builder.RegisterService(&callback_service_);

    server_ = builder.BuildAndStart();
  }

  void DestroyServer() {
    if (server_) {
      server_->Shutdown();
      server_.reset();
    }
  }

  void ResetStub() {
    ChannelArguments args;
    auto channel_creds = GetCredentialsProvider()->GetChannelCredentials(
        GetParam().credentials_type, &args);
    switch (GetParam().protocol) {
      case Protocol::TCP:
        channel_ = grpc::CreateCustomChannel(server_address_.str(),
                                             channel_creds, args);
        break;
      case Protocol::INPROC:
        channel_ = server_->InProcessChannel(args);
        break;
      default:
        assert(false);
    }
    stub_ = EchoTestService::NewStub(channel_);
  }

  void TearDown() override {
    DestroyServer();
    if (picked_port_ > 0) {
      grpc_recycle_unused_port(picked_port_);
    }
  }

  void SendRpcs(int num_rpcs) {
    std::string test_string;
    for (int i = 0; i < num_rpcs; i++) {
      EchoRequest request;
      EchoResponse response;
      ClientContext cli_ctx;

      test_string += std::string(1024, 'x');
      request.set_message(test_string);
      std::string val;
      cli_ctx.set_compression_algorithm(GRPC_COMPRESS_GZIP);

      std::mutex mu;
      std::condition_variable cv;
      bool done = false;
      stub_->async()->Echo(
          &cli_ctx, &request, &response,
          [&request, &response, &done, &mu, &cv, val](Status s) {
            CHECK(s.ok());

            EXPECT_EQ(request.message(), response.message());
            std::lock_guard<std::mutex> l(mu);
            done = true;
            cv.notify_one();
          });
      std::unique_lock<std::mutex> l(mu);
      while (!done) {
        cv.wait(l);
      }
    }
  }

  int picked_port_{0};
  std::shared_ptr<Channel> channel_;
  std::unique_ptr<EchoTestService::Stub> stub_;
  CallbackTestServiceImpl callback_service_;
  std::unique_ptr<Server> server_;
  std::ostringstream server_address_;
};

class NullAllocatorTest : public MessageAllocatorEnd2endTestBase {};

TEST_P(NullAllocatorTest, SimpleRpc) {
  CreateServer(nullptr);
  ResetStub();
  SendRpcs(1);
}

class SimpleAllocatorTest : public MessageAllocatorEnd2endTestBase {
 public:
  class SimpleAllocator : public MessageAllocator<EchoRequest, EchoResponse> {
   public:
    class MessageHolderImpl : public MessageHolder<EchoRequest, EchoResponse> {
     public:
      MessageHolderImpl(std::atomic_int* request_deallocation_count,
                        std::atomic_int* messages_deallocation_count)
          : request_deallocation_count_(request_deallocation_count),
            messages_deallocation_count_(messages_deallocation_count) {
        set_request(new EchoRequest);
        set_response(new EchoResponse);
      }
      void Release() override {
        (*messages_deallocation_count_)++;
        delete request();
        delete response();
        delete this;
      }
      void FreeRequest() override {
        (*request_deallocation_count_)++;
        delete request();
        set_request(nullptr);
      }

      EchoRequest* ReleaseRequest() {
        auto* ret = request();
        set_request(nullptr);
        return ret;
      }

     private:
      std::atomic_int* const request_deallocation_count_;
      std::atomic_int* const messages_deallocation_count_;
    };
    MessageHolder<EchoRequest, EchoResponse>* AllocateMessages() override {
      allocation_count++;
      return new MessageHolderImpl(&request_deallocation_count,
                                   &messages_deallocation_count);
    }
    int allocation_count = 0;
    std::atomic_int request_deallocation_count{0};
    std::atomic_int messages_deallocation_count{0};
  };
};

TEST_P(SimpleAllocatorTest, SimpleRpc) {
  const int kRpcCount = 10;
  std::unique_ptr<SimpleAllocator> allocator(new SimpleAllocator);
  CreateServer(allocator.get());
  ResetStub();
  SendRpcs(kRpcCount);
  // messages_deallocaton_count is updated in Release after server side OnDone.
  // Destroy server to make sure it has been updated.
  DestroyServer();
  EXPECT_EQ(kRpcCount, allocator->allocation_count);
  EXPECT_EQ(kRpcCount, allocator->messages_deallocation_count);
  EXPECT_EQ(0, allocator->request_deallocation_count);
}

TEST_P(SimpleAllocatorTest, RpcWithEarlyFreeRequest) {
  const int kRpcCount = 10;
  std::unique_ptr<SimpleAllocator> allocator(new SimpleAllocator);
  auto mutator = [](RpcAllocatorState* allocator_state, const EchoRequest* req,
                    EchoResponse* resp) {
    auto* info =
        static_cast<SimpleAllocator::MessageHolderImpl*>(allocator_state);
    EXPECT_EQ(req, info->request());
    EXPECT_EQ(resp, info->response());
    allocator_state->FreeRequest();
    EXPECT_EQ(nullptr, info->request());
  };
  callback_service_.SetAllocatorMutator(mutator);
  CreateServer(allocator.get());
  ResetStub();
  SendRpcs(kRpcCount);
  // messages_deallocaton_count is updated in Release after server side OnDone.
  // Destroy server to make sure it has been updated.
  DestroyServer();
  EXPECT_EQ(kRpcCount, allocator->allocation_count);
  EXPECT_EQ(kRpcCount, allocator->messages_deallocation_count);
  EXPECT_EQ(kRpcCount, allocator->request_deallocation_count);
}

TEST_P(SimpleAllocatorTest, RpcWithReleaseRequest) {
  const int kRpcCount = 10;
  std::unique_ptr<SimpleAllocator> allocator(new SimpleAllocator);
  std::vector<EchoRequest*> released_requests;
  auto mutator = [&released_requests](RpcAllocatorState* allocator_state,
                                      const EchoRequest* req,
                                      EchoResponse* resp) {
    auto* info =
        static_cast<SimpleAllocator::MessageHolderImpl*>(allocator_state);
    EXPECT_EQ(req, info->request());
    EXPECT_EQ(resp, info->response());
    released_requests.push_back(info->ReleaseRequest());
    EXPECT_EQ(nullptr, info->request());
  };
  callback_service_.SetAllocatorMutator(mutator);
  CreateServer(allocator.get());
  ResetStub();
  SendRpcs(kRpcCount);
  // messages_deallocaton_count is updated in Release after server side OnDone.
  // Destroy server to make sure it has been updated.
  DestroyServer();
  EXPECT_EQ(kRpcCount, allocator->allocation_count);
  EXPECT_EQ(kRpcCount, allocator->messages_deallocation_count);
  EXPECT_EQ(0, allocator->request_deallocation_count);
  EXPECT_EQ(static_cast<unsigned>(kRpcCount), released_requests.size());
  for (auto* req : released_requests) {
    delete req;
  }
}

class ArenaAllocatorTest : public MessageAllocatorEnd2endTestBase {
 public:
  class ArenaAllocator : public MessageAllocator<EchoRequest, EchoResponse> {
   public:
    class MessageHolderImpl : public MessageHolder<EchoRequest, EchoResponse> {
     public:
      MessageHolderImpl() {
        set_request(google::protobuf::Arena::Create<EchoRequest>(&arena_));
        set_response(google::protobuf::Arena::Create<EchoResponse>(&arena_));
      }
      void Release() override { delete this; }
      void FreeRequest() override { CHECK(0); }

     private:
      google::protobuf::Arena arena_;
    };
    MessageHolder<EchoRequest, EchoResponse>* AllocateMessages() override {
      allocation_count++;
      return new MessageHolderImpl;
    }
    int allocation_count = 0;
  };
};

TEST_P(ArenaAllocatorTest, SimpleRpc) {
  const int kRpcCount = 10;
  std::unique_ptr<ArenaAllocator> allocator(new ArenaAllocator);
  CreateServer(allocator.get());
  ResetStub();
  SendRpcs(kRpcCount);
  EXPECT_EQ(kRpcCount, allocator->allocation_count);
}

std::vector<TestScenario> CreateTestScenarios(bool test_insecure) {
  std::vector<TestScenario> scenarios;
  std::vector<std::string> credentials_types{
      GetCredentialsProvider()->GetSecureCredentialsTypeList()};
  auto insec_ok = [] {
    // Only allow insecure credentials type when it is registered with the
    // provider. User may create providers that do not have insecure.
    return GetCredentialsProvider()->GetChannelCredentials(
               kInsecureCredentialsType, nullptr) != nullptr;
  };
  if (test_insecure && insec_ok()) {
    credentials_types.push_back(kInsecureCredentialsType);
  }
  CHECK(!credentials_types.empty());

  Protocol parr[]{Protocol::INPROC, Protocol::TCP};
  for (Protocol p : parr) {
    for (const auto& cred : credentials_types) {
      // TODO(vjpai): Test inproc with secure credentials when feasible
      if (p == Protocol::INPROC &&
          (cred != kInsecureCredentialsType || !insec_ok())) {
        continue;
      }
      scenarios.emplace_back(p, cred);
    }
  }
  return scenarios;
}

INSTANTIATE_TEST_SUITE_P(NullAllocatorTest, NullAllocatorTest,
                         ::testing::ValuesIn(CreateTestScenarios(true)));
INSTANTIATE_TEST_SUITE_P(SimpleAllocatorTest, SimpleAllocatorTest,
                         ::testing::ValuesIn(CreateTestScenarios(true)));
INSTANTIATE_TEST_SUITE_P(ArenaAllocatorTest, ArenaAllocatorTest,
                         ::testing::ValuesIn(CreateTestScenarios(true)));

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
