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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "src/core/handshaker/security/secure_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/extensions/receive_coalescing_extension.h"
#include "src/core/lib/event_engine/query_extensions.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/orphanable.h"
#include "test/core/test_util/mock_endpoint.h"
#include "gtest/gtest.h"
#include "absl/synchronization/notification.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::MockEndpointController;

namespace grpc_core {
namespace {

// Protects a message with a fake frame protector so that it can be fed to the
// secure endpoint as if it arrived on the wire.
std::string ProtectFrame(tsi_frame_protector* protector,
                         const std::string& plaintext) {
  std::string result;
  const unsigned char* msg =
      reinterpret_cast<const unsigned char*>(plaintext.data());
  size_t msg_len = plaintext.size();
  while (msg_len > 0) {
    unsigned char buf[4096];
    size_t processed_msg_size = msg_len;
    size_t protected_buf_size = sizeof(buf);
    tsi_result res = tsi_frame_protector_protect(
        protector, msg, &processed_msg_size, buf, &protected_buf_size);
    EXPECT_EQ(res, TSI_OK);
    if (res != TSI_OK) break;
    result.append(reinterpret_cast<char*>(buf), protected_buf_size);
    msg += processed_msg_size;
    msg_len -= processed_msg_size;
  }
  size_t still_pending = 0;
  do {
    unsigned char buf[4096];
    size_t protected_buf_size = sizeof(buf);
    tsi_result res = tsi_frame_protector_protect_flush(
        protector, buf, &protected_buf_size, &still_pending);
    EXPECT_EQ(res, TSI_OK);
    if (res != TSI_OK) break;
    result.append(reinterpret_cast<char*>(buf), protected_buf_size);
  } while (still_pending > 0);
  return result;
}

class SecureEndpointShutdownRaceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
    mock_ctrl_ = MockEndpointController::Create(engine_);
    fake_protector_ = tsi_create_fake_frame_protector(nullptr);
    fake_protector_for_encryption_ = tsi_create_fake_frame_protector(nullptr);
    ChannelArgs args = ChannelArgs().SetObject(ResourceQuota::Default());
    grpc_endpoint* wrapped_mock_ep = mock_ctrl_->TakeCEndpoint();
    auto secure_ep = grpc_secure_endpoint_create(
        fake_protector_, nullptr, OrphanablePtr<grpc_endpoint>(wrapped_mock_ep),
        nullptr, 0, args);
    secure_ep_ = grpc_event_engine::experimental::
        grpc_take_wrapped_event_engine_endpoint(secure_ep.release());
    auto* ext = grpc_event_engine::experimental::QueryExtension<
        grpc_event_engine::experimental::ReceiveCoalescingExtension>(
        secure_ep_.get());
    ASSERT_NE(ext, nullptr);
    ext->EnableRpcReceiveCoalescing();
  }

  void TearDown() override {
    secure_ep_.reset();
    tsi_frame_protector_destroy(fake_protector_for_encryption_);
  }

  std::shared_ptr<EventEngine> engine_;
  std::shared_ptr<MockEndpointController> mock_ctrl_;
  tsi_frame_protector* fake_protector_;  // owned by secure_ep_
  tsi_frame_protector* fake_protector_for_encryption_;
  std::unique_ptr<EventEngine::Endpoint> secure_ep_;
};

// A read that is being re-armed when the endpoint is shut down must complete
// with an error rather than racing the teardown. The read machinery
// (ContinueRead) re-arms reads from read callbacks running on event engine
// threads while the test thread destroys the endpoint; with receive coalescing
// enabled and a hint larger than any single frame, the read keeps accumulating
// and re-arming until shutdown.
TEST_F(SecureEndpointShutdownRaceTest, ReadRacingShutdownCompletesWithError) {
  absl::Notification read_done;
  absl::Status read_status;
  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(1 << 20);
  secure_ep_->Read(
      [&read_done, &read_status](absl::Status s) {
        read_status = s;
        read_done.Notify();
      },
      &read_buffer, args);
  // Keep the read loop busy re-arming reads, then destroy the endpoint in the
  // middle of it.
  std::thread feeder([&]() {
    for (int i = 0; i < 100000; ++i) {
      mock_ctrl_->TriggerReadEvent(
          grpc_event_engine::experimental::Slice::FromCopiedString(
              ProtectFrame(fake_protector_for_encryption_, "payload")));
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  secure_ep_.reset();
  ASSERT_TRUE(read_done.WaitForNotificationWithTimeout(absl::Seconds(30)));
  EXPECT_FALSE(read_status.ok());
  feeder.join();
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("secure_endpoint_read_coalescing", true);
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
