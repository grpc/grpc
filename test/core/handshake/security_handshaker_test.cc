// Copyright 2026 gRPC authors.
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

#include "src/core/handshaker/security/security_handshaker.h"
#include "src/core/handshaker/security/security_telemetry.h"
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <stdlib.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/tsi/fake_transport_security.h"
#include "test/core/test_util/mock_endpoint.h"
#include <grpc/event_engine/event_engine.h>
#include "absl/synchronization/notification.h"

namespace grpc_core {
namespace {

using grpc_event_engine::experimental::MockEndpointController;
using grpc_event_engine::experimental::GetDefaultEventEngine;

class MockSecurityConnector : public grpc_security_connector {
 public:
  MockSecurityConnector() : grpc_security_connector("https") {}
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("mock");
    return kFactory.Create();
  }
  void check_peer(tsi_peer peer, grpc_endpoint* ep, const ChannelArgs& args,
                  RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    abort();
  }
  void cancel_check_peer(grpc_closure* on_peer_checked, grpc_error_handle error) override {}
  int cmp(const grpc_security_connector* other) const override { return 0; }
};

TEST(SecurityHandshakerTest, MetricEmissionOnFailure) {
  ExecCtx exec_ctx; // Needed for gRPC core operations
  
  // Register hook
  bool hook_called = false;
  RegisterHistogramCollectionHook([&hook_called](const InstrumentMetadata::Description* instrument,
                                                  absl::Span<const std::string> labels, int64_t value) {
    if (instrument->name == "grpc.security.handshaker.duration") {
      hook_called = true;
      // We expect 3 labels: status, error_details, protocol
      ASSERT_EQ(labels.size(), 3);
      // Status should be non-OK, protocol should be "fake" (from tsi_create_fake_handshaker)
      EXPECT_EQ(labels[2], "mock");
    }
  });

  // Create mock endpoint to avoid segfaults on read/write
  auto engine = GetDefaultEventEngine();
  auto mock_ctrl = MockEndpointController::Create(engine);
  grpc_endpoint* mock_ep = mock_ctrl->TakeCEndpoint();

  // Create handshaker using fake TSI handshaker
  auto connector = MakeRefCounted<MockSecurityConnector>();
  auto handshaker = SecurityHandshakerCreate(tsi_create_fake_handshaker(1 /* is_client */), connector.get(), ChannelArgs());

  // Force read failure to trigger handshake failure and metric emission
  mock_ctrl->NoMoreReads();

  // Drive handshake
  HandshakerArgs args;
  args.args = ChannelArgs();
  args.endpoint = OrphanablePtr<grpc_endpoint>(mock_ep);
  args.event_engine = engine.get();
  
  absl::Notification done;
  handshaker->DoHandshake(&args, [&done](absl::Status status) {
    done.Notify();
  });

  // Wait for it to complete. It should fail due to NoMoreReads.
  // We give it a timeout to avoid hangs if it stalls.
  EXPECT_TRUE(done.WaitForNotificationWithTimeout(absl::Seconds(5)));
  EXPECT_TRUE(hook_called);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_init(); // Needed to initialize gRPC types and systems
  ::testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
