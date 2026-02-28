//
//
// Copyright 2025 gRPC authors.
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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "src/core/handshaker/security/secure_endpoint.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"
#include "src/core/util/orphanable.h"
#include "test/core/test_util/mock_endpoint.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"

using grpc_event_engine::experimental::EventEngine;
using grpc_event_engine::experimental::MockEndpointController;

namespace grpc_core {
namespace {

std::string Protect(tsi_frame_protector* protector,
                    std::string_view plaintext) {
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

class SecureEndpointReadCoalescingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    engine_ = grpc_event_engine::experimental::GetDefaultEventEngine();
    mock_ctrl_ = MockEndpointController::Create(engine_);

    // Create protectors
    fake_protector_ = tsi_create_fake_frame_protector(nullptr);
    fake_protector_for_encryption_ = tsi_create_fake_frame_protector(nullptr);

    ChannelArgs args = ChannelArgs().SetObject(ResourceQuota::Default());

    grpc_endpoint* wrapped_mock_ep = mock_ctrl_->TakeCEndpoint();

    auto secure_ep_grpc = grpc_secure_endpoint_create(
        fake_protector_, nullptr, OrphanablePtr<grpc_endpoint>(wrapped_mock_ep),
        nullptr, 0, args);

    secure_ep_ = grpc_event_engine::experimental::
        grpc_take_wrapped_event_engine_endpoint(secure_ep_grpc.release());
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

TEST_F(SecureEndpointReadCoalescingTest, ReadCoalescingSatisfiesHint) {
  std::string pt1 = "hello ";
  std::string pt2 = "world";
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);
  std::string ct2 = Protect(fake_protector_for_encryption_, pt2);

  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1));
  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct2));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(pt1.size() + pt2.size());

  absl::Notification read_done;
  absl::Status read_status;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        read_status = s;
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) {
    read_done.WaitForNotification();
  }

  EXPECT_TRUE(read_status.ok());
  std::string result;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result, pt1 + pt2);
}

TEST_F(SecureEndpointReadCoalescingTest, ReadLeftoversPreserved) {
  std::string pt1 = "part1";
  std::string pt2 = "part2";
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);
  std::string ct2 = Protect(fake_protector_for_encryption_, pt2);

  // Trigger both frames in transport
  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1));
  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct2));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(pt1.size());

  absl::Notification read_done1;
  bool immediate1 = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done1.Notify();
      },
      &read_buffer, args);

  if (!immediate1) read_done1.WaitForNotification();

  std::string result1;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result1.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result1, pt1);
  read_buffer.Clear();

  // Read again, it should use leftovers
  args.set_read_hint_bytes(pt2.size());
  absl::Notification read_done2;
  bool immediate2 = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done2.Notify();
      },
      &read_buffer, args);

  if (!immediate2) read_done2.WaitForNotification();

  std::string result2;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result2.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result2, pt2);
}

TEST_F(SecureEndpointReadCoalescingTest, StallPrevention) {
  std::string pt1 = "frame1";
  std::string pt2 = "frame2";
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);
  std::string ct2 = Protect(fake_protector_for_encryption_, pt2);

  // Send both frames together (they might be processed together if read
  // coalescing is aggressive)
  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1 + ct2));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(pt1.size());

  absl::Notification read_done1;
  bool immediate1 = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done1.Notify();
      },
      &read_buffer, args);

  if (!immediate1) read_done1.WaitForNotification();

  std::string result1;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result1.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result1, pt1);
  read_buffer.Clear();

  // Now, second read. Since ciphertext is already in the secure endpoint, it
  // shouldn't stall on the transport!
  args.set_read_hint_bytes(pt2.size());
  absl::Notification read_done2;
  bool immediate2 = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done2.Notify();
      },
      &read_buffer, args);

  if (!immediate2) read_done2.WaitForNotification();

  std::string result2;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result2.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result2, pt2);
}

TEST_F(SecureEndpointReadCoalescingTest, ReadHintSmallerThanFirstFrame) {
  std::string pt1 = "hello world";
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);

  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(5);  // Smaller than first frame

  absl::Notification read_done;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) read_done.WaitForNotification();

  // We should still get the whole first frame because we have to decrypt
  // the entire frame to read any of it
  std::string result;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result, pt1);
}

TEST_F(SecureEndpointReadCoalescingTest, ReadHintSpansFrames) {
  std::string pt1 = "frame1";
  std::string pt2 = "frame2";
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);
  std::string ct2 = Protect(fake_protector_for_encryption_, pt2);

  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1));
  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct2));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(pt1.size() + 2);  // Spans into second frame

  absl::Notification read_done;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) read_done.WaitForNotification();

  // We should get both frames because the hint required a byte from the
  // second frame, meaning it had to be decrypted.
  std::string result;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result, pt1 + pt2);
}

TEST_F(SecureEndpointReadCoalescingTest, ReadHintZero) {
  std::string pt1 = "frame1";
  std::string pt2 = "frame2";
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);
  std::string ct2 = Protect(fake_protector_for_encryption_, pt2);

  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1 + ct2));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(0);

  absl::Notification read_done;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) read_done.WaitForNotification();

  // If a read hint of 0 is provided, the secure endpoint disables coalescing,
  // bypassing the explicit block on a specific buffer length and behaves
  // eagerly, successfully reading and unprotecting any data available.
  std::string result;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result, pt1 + pt2);
}

TEST_F(SecureEndpointReadCoalescingTest, LargeFrameExceedingStagingBuffer) {
  // 10000 bytes > 8192 staging buffer
  std::string pt1(10000, 'A');
  std::string ct1 = Protect(fake_protector_for_encryption_, pt1);

  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(ct1));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(pt1.size());

  absl::Notification read_done;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) read_done.WaitForNotification();

  std::string result;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result, pt1);
}

TEST_F(SecureEndpointReadCoalescingTest, LargeHintManySmallFrames) {
  std::vector<std::string> pts;
  std::string combined_pt;
  std::string combined_ct;

  for (int i = 0; i < 100; ++i) {
    std::string pt = "frame" + std::to_string(i);
    pts.push_back(pt);
    combined_pt += pt;
    combined_ct += Protect(fake_protector_for_encryption_, pt);
  }

  mock_ctrl_->TriggerReadEvent(
      grpc_event_engine::experimental::Slice::FromCopiedString(combined_ct));

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;
  args.set_read_hint_bytes(combined_pt.size());

  absl::Notification read_done;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        EXPECT_TRUE(s.ok());
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) read_done.WaitForNotification();

  std::string result;
  for (size_t i = 0; i < read_buffer.Count(); ++i) {
    result.append(std::string(read_buffer.RefSlice(i).as_string_view()));
  }
  EXPECT_EQ(result, combined_pt);
}

TEST_F(SecureEndpointReadCoalescingTest, TransportReadError) {
  mock_ctrl_->NoMoreReads();

  grpc_event_engine::experimental::SliceBuffer read_buffer;
  EventEngine::Endpoint::ReadArgs args;

  absl::Notification read_done;
  absl::Status read_status;
  bool immediate = secure_ep_->Read(
      [&](absl::Status s) {
        read_status = s;
        read_done.Notify();
      },
      &read_buffer, args);

  if (!immediate) read_done.WaitForNotification();

  // We should get some kind of error from the NoMoreReads emulation
  EXPECT_FALSE(read_status.ok());
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("secure_endpoint_read_coalescing", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
