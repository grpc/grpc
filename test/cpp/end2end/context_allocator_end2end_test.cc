//
//
// Copyright 2020 gRPC authors.
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
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/test_credentials_provider.h"

namespace grpc {
namespace testing {
namespace {

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

class ContextAllocatorEnd2endTestBase
    : public ::testing::TestWithParam<TestScenario> {
 protected:
  static void SetUpTestSuite() { grpc_init(); }
  static void TearDownTestSuite() { grpc_shutdown(); }
  ContextAllocatorEnd2endTestBase() {}

  ~ContextAllocatorEnd2endTestBase() override = default;

  void SetUp() override { GetParam().Log(); }

  void CreateServer(std::unique_ptr<grpc::ContextAllocator> context_allocator) {
    ServerBuilder builder;

    auto server_creds = GetCredentialsProvider()->GetServerCredentials(
        GetParam().credentials_type);
    if (GetParam().protocol == Protocol::TCP) {
      picked_port_ = grpc_pick_unused_port_or_die();
      server_address_ << "localhost:" << picked_port_;
      builder.AddListeningPort(server_address_.str(), server_creds);
    }
    builder.SetContextAllocator(std::move(context_allocator));
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

class DefaultContextAllocatorTest : public ContextAllocatorEnd2endTestBase {};

TEST_P(DefaultContextAllocatorTest, SimpleRpc) {
  const int kRpcCount = 10;
  CreateServer(nullptr);
  ResetStub();
  SendRpcs(kRpcCount);
}

class NullContextAllocatorTest : public ContextAllocatorEnd2endTestBase {
 public:
  class NullAllocator : public grpc::ContextAllocator {
   public:
    NullAllocator(std::atomic<int>* allocation_count,
                  std::atomic<int>* deallocation_count)
        : allocation_count_(allocation_count),
          deallocation_count_(deallocation_count) {}
    grpc::CallbackServerContext* NewCallbackServerContext() override {
      allocation_count_->fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }

    GenericCallbackServerContext* NewGenericCallbackServerContext() override {
      allocation_count_->fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }

    void Release(
        grpc::CallbackServerContext* /*callback_server_context*/) override {
      deallocation_count_->fetch_add(1, std::memory_order_relaxed);
    }

    void Release(
        GenericCallbackServerContext* /*generic_callback_server_context*/)
        override {
      deallocation_count_->fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<int>* allocation_count_;
    std::atomic<int>* deallocation_count_;
  };
};

TEST_P(NullContextAllocatorTest, UnaryRpc) {
  const int kRpcCount = 10;
  std::atomic<int> allocation_count{0};
  std::atomic<int> deallocation_count{0};
  std::unique_ptr<NullAllocator> allocator(
      new NullAllocator(&allocation_count, &deallocation_count));
  CreateServer(std::move(allocator));
  ResetStub();
  SendRpcs(kRpcCount);
  // messages_deallocaton_count is updated in Release after server side
  // OnDone.
  DestroyServer();
  EXPECT_EQ(kRpcCount, allocation_count);
  EXPECT_EQ(kRpcCount, deallocation_count);
}

class SimpleContextAllocatorTest : public ContextAllocatorEnd2endTestBase {
 public:
  class SimpleAllocator : public grpc::ContextAllocator {
   public:
    SimpleAllocator(std::atomic<int>* allocation_count,
                    std::atomic<int>* deallocation_count)
        : allocation_count_(allocation_count),
          deallocation_count_(deallocation_count) {}
    grpc::CallbackServerContext* NewCallbackServerContext() override {
      allocation_count_->fetch_add(1, std::memory_order_relaxed);
      return new grpc::CallbackServerContext();
    }
    GenericCallbackServerContext* NewGenericCallbackServerContext() override {
      allocation_count_->fetch_add(1, std::memory_order_relaxed);
      return new GenericCallbackServerContext();
    }

    void Release(
        grpc::CallbackServerContext* callback_server_context) override {
      deallocation_count_->fetch_add(1, std::memory_order_relaxed);
      delete callback_server_context;
    }

    void Release(GenericCallbackServerContext* generic_callback_server_context)
        override {
      deallocation_count_->fetch_add(1, std::memory_order_relaxed);
      delete generic_callback_server_context;
    }

    std::atomic<int>* allocation_count_;
    std::atomic<int>* deallocation_count_;
  };
};

TEST_P(SimpleContextAllocatorTest, UnaryRpc) {
  const int kRpcCount = 10;
  std::atomic<int> allocation_count{0};
  std::atomic<int> deallocation_count{0};
  std::unique_ptr<SimpleAllocator> allocator(
      new SimpleAllocator(&allocation_count, &deallocation_count));
  CreateServer(std::move(allocator));
  ResetStub();
  SendRpcs(kRpcCount);
  // messages_deallocaton_count is updated in Release after server side
  // OnDone.
  DestroyServer();
  EXPECT_EQ(kRpcCount, allocation_count);
  EXPECT_EQ(kRpcCount, deallocation_count);
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
      if (p == Protocol::INPROC &&
          (cred != kInsecureCredentialsType || !insec_ok())) {
        continue;
      }
      scenarios.emplace_back(p, cred);
    }
  }
  return scenarios;
}

// TODO(ddyihai): adding client streaming/server streaming/bidi streaming
// test.

INSTANTIATE_TEST_SUITE_P(DefaultContextAllocatorTest,
                         DefaultContextAllocatorTest,
                         ::testing::ValuesIn(CreateTestScenarios(true)));
INSTANTIATE_TEST_SUITE_P(NullContextAllocatorTest, NullContextAllocatorTest,
                         ::testing::ValuesIn(CreateTestScenarios(true)));
INSTANTIATE_TEST_SUITE_P(SimpleContextAllocatorTest, SimpleContextAllocatorTest,
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
