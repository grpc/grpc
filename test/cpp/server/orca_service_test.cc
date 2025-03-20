//
// Copyright 2024 gRPC authors.
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

#include "src/cpp/server/orca/orca_service.h"

#include <grpc/grpc.h>
#include <grpcpp/ext/orca_service.h>
#include <grpcpp/ext/server_metric_recorder.h>
#include <grpcpp/support/byte_buffer.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/support/slice.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "src/core/util/notification.h"
#include "test/core/test_util/test_config.h"

namespace grpc {
namespace testing {

using experimental::OrcaService;
using experimental::ServerMetricRecorder;

class OrcaServiceTest : public ::testing::Test {
 public:
  OrcaServiceTest()
      : server_metric_recorder_(ServerMetricRecorder::Create()),
        orca_service_(server_metric_recorder_.get(),
                      OrcaService::Options().set_min_report_duration(
                          absl::ZeroDuration())) {};
  ~OrcaServiceTest() override = default;

  class TestReactorHook : public OrcaService::ReactorHook {
   public:
    explicit TestReactorHook(grpc::Status expected_status)
        : expected_status_(expected_status) {}
    void OnFinish(grpc::Status status) override {
      EXPECT_EQ(status.error_code(), expected_status_.error_code());
      EXPECT_EQ(status.error_message(), expected_status_.error_message());
      notification_.Notify();
    }

    void OnStartWrite(const ByteBuffer* /*response*/) override {
      GTEST_FAIL() << "Unexpected write of response";
    }

    void AwaitFinish() { notification_.WaitForNotification(); }

   private:
    grpc_core::Notification notification_;
    grpc::Status expected_status_;
  };

 protected:
  std::unique_ptr<ServerWriteReactor<ByteBuffer>> InstantiateReactor(
      absl::string_view peer, const ByteBuffer* request_buffer,
      std::shared_ptr<TestReactorHook> hook) {
    return std::make_unique<OrcaService::Reactor>(
        &orca_service_, peer, request_buffer, std::move(hook));
  }

 private:
  std::unique_ptr<ServerMetricRecorder> server_metric_recorder_;
  OrcaService orca_service_;
};

TEST_F(OrcaServiceTest, ReactorEmptyInputBufferTest) {
  std::shared_ptr<TestReactorHook> hook = std::make_shared<TestReactorHook>(
      grpc::Status(grpc::StatusCode::INTERNAL, "Buffer not initialized"));
  ByteBuffer request_buffer;
  auto reactor = InstantiateReactor("peer", &request_buffer, hook);
  hook->AwaitFinish();
}

TEST_F(OrcaServiceTest, ReactorCorruptBufferTest) {
  std::shared_ptr<TestReactorHook> hook =
      std::make_shared<TestReactorHook>(grpc::Status(
          grpc::StatusCode::INTERNAL, "could not parse request proto"));
  Slice data("Hello World");
  ByteBuffer request_buffer(&data, 1);
  auto reactor = InstantiateReactor("peer", &request_buffer, hook);
  hook->AwaitFinish();
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
