
#include "test/core/xds/xds_transport_fake.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/util/orphanable.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "absl/log/log.h" 
#include "grpc/grpc.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class FakeXdsServerTarget : public XdsBootstrap::XdsServerTarget {
 public:
  explicit FakeXdsServerTarget(std::string uri) : uri_(std::move(uri)) {}
  const std::string& server_uri() const override { return uri_; }
  std::string Key() const override { return uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    return Key() == other.Key();
  }
 private:
  std::string uri_;
};

class FakeXdsTransportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto event_engine = std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
        grpc_event_engine::experimental::FuzzingEventEngine::Options(),
        fuzzing_event_engine::Actions());
    factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { LOG(FATAL) << "Too many pending reads"; },
        event_engine);
  }

  void TearDown() override {
    factory_.reset();
  }

  RefCountedPtr<FakeXdsTransportFactory> factory_;
  FakeXdsServerTarget server_{"server_uri"};
};

TEST_F(FakeXdsTransportTest, UnaryCallSuccess) {
  auto transport = factory_->GetTransport(server_, nullptr);
  ASSERT_NE(transport, nullptr);

  std::thread t([transport]() {
    auto call = transport->CreateUnaryCall("method");
    auto result = call->SendMessage("payload");
    if (result.ok()) {
      EXPECT_EQ(*result, "response");
    } else {
      ADD_FAILURE() << "SendMessage failed: " << result.status();
    }
  });

  RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> fake_call;
  for (int i = 0; i < 50; ++i) {
    fake_call = factory_->WaitForUnaryCall(server_, "method");
    if (fake_call != nullptr) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ASSERT_NE(fake_call, nullptr);
  auto request = fake_call->WaitForMessageFromClient();
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(*request, "payload");

  fake_call->SendMessageToClient("response");
  t.join();
}

TEST_F(FakeXdsTransportTest, UnaryCallFailure) {
  auto transport = factory_->GetTransport(server_, nullptr);
  ASSERT_NE(transport, nullptr);

  std::thread t([transport]() {
    auto call = transport->CreateUnaryCall("method");
    auto result = call->SendMessage("payload");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  });

  RefCountedPtr<FakeXdsTransportFactory::FakeUnaryCall> fake_call;
  for (int i = 0; i < 50; ++i) {
    fake_call = factory_->WaitForUnaryCall(server_, "method");
    if (fake_call != nullptr) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ASSERT_NE(fake_call, nullptr);
  auto request = fake_call->WaitForMessageFromClient();
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(*request, "payload");

  fake_call->MaybeSendStatusToClient(absl::InternalError("failure"));
  t.join();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
