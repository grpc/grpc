//
//
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
//
//

// Tests for SECURITY frame handling in the legacy chttp2 transport
// (grpc_chttp2_transport) with an endpoint that implements
// TransportFramingEndpointExtension.

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/grpc.h>
#include <grpc/impl/channel_arg_names.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport_framing_endpoint_extension.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace {

using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::URIToResolvedAddress;

constexpr absl::Duration kTimeout = absl::Seconds(10);

// Server SETTINGS frame advertising GRPC_ALLOW_SECURITY_FRAME (0xfe05) = 1.
constexpr absl::string_view kSettingsAllowSecurityFrame(
    "\x00\x00\x06\x04\x00\x00\x00\x00\x00"
    "\xfe\x05\x00\x00\x00\x01",
    15);
// Empty server SETTINGS frame (peer does not allow SECURITY frames).
constexpr absl::string_view kEmptySettings(
    "\x00\x00\x00\x04\x00\x00\x00\x00\x00", 9);

// Returns the serialized SECURITY frame (type 200) carrying `payload`.
std::string SecurityFrame(absl::string_view payload) {
  std::string frame;
  frame.push_back(static_cast<char>((payload.size() >> 16) & 0xff));
  frame.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
  frame.push_back(static_cast<char>(payload.size() & 0xff));
  frame.push_back(static_cast<char>(200));  // type
  frame.push_back(0);                       // flags
  frame.append(4, '\0');                    // stream id
  frame.append(payload.data(), payload.size());
  return frame;
}

// State shared between the test, the fake endpoint and its framing extension.
// It outlives the endpoint, which the transport destroys when it closes.
class FakeEndpointState {
 public:
  using SendFrameCallback = absl::AnyInvocable<void(SliceBuffer)>;

  // --- TransportFramingEndpointExtension ---
  void SetSendFrameCallback(SendFrameCallback cb) {
    std::shared_ptr<SendFrameCallback> old_cb;
    {
      MutexLock lock(mu_);
      old_cb = std::move(send_frame_cb_);
      if (cb == nullptr) {
        ++null_callback_count_;
      } else {
        send_frame_cb_ = std::make_shared<SendFrameCallback>(std::move(cb));
      }
      cv_.SignalAll();
    }
    // Dropping the old callback releases its transport ref outside `mu_`.
  }

  // Invokes the registered send-frame callback.
  void SendFrame(absl::string_view payload) {
    std::shared_ptr<SendFrameCallback> cb;
    {
      MutexLock lock(mu_);
      cb = send_frame_cb_;
    }
    ASSERT_NE(cb, nullptr);
    SliceBuffer data;
    data.Append(Slice::FromCopiedString(payload));
    (*cb)(std::move(data));
  }

  bool has_send_frame_callback() {
    MutexLock lock(mu_);
    return send_frame_cb_ != nullptr;
  }

  bool WaitForNullCallback() {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + kTimeout;
    while (null_callback_count_ == 0) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return null_callback_count_ > 0;
  }

  // --- EventEngine::Endpoint ---
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer) {
    MutexLock lock(mu_);
    on_read_ = std::move(on_read);
    read_buffer_ = buffer;
    cv_.SignalAll();
    return false;
  }

  bool Write(grpc_event_engine::experimental::SliceBuffer* data) {
    MutexLock lock(mu_);
    for (size_t i = 0; i < data->Count(); ++i) {
      written_.append((*data)[i].as_string_view());
    }
    data->Clear();
    cv_.SignalAll();
    return true;
  }

  // Completes the pending transport read with `bytes`.
  void DeliverRead(absl::string_view bytes) {
    absl::AnyInvocable<void(absl::Status)> on_read;
    {
      MutexLock lock(mu_);
      const absl::Time deadline = absl::Now() + kTimeout;
      while (on_read_ == nullptr) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) break;
      }
      ASSERT_NE(on_read_, nullptr) << "Transport never issued a read";
      read_buffer_->Append(
          grpc_event_engine::experimental::Slice::FromCopiedString(bytes));
      on_read = std::move(on_read_);
      on_read_ = nullptr;
      read_buffer_ = nullptr;
    }
    on_read(absl::OkStatus());
  }

  // Called when the endpoint is destroyed: fails any pending read (as the
  // EventEngine contract requires) and records the destruction.
  void OnEndpointDestroyed() {
    absl::AnyInvocable<void(absl::Status)> on_read;
    {
      MutexLock lock(mu_);
      on_read = std::move(on_read_);
      on_read_ = nullptr;
      read_buffer_ = nullptr;
      endpoint_destroyed_ = true;
      cv_.SignalAll();
    }
    if (on_read != nullptr) {
      GetDefaultEventEngine()->Run([on_read = std::move(on_read)]() mutable {
        on_read(absl::CancelledError("endpoint destroyed"));
      });
    }
  }

  bool WaitForEndpointDestroyed() {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + kTimeout;
    while (!endpoint_destroyed_) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return endpoint_destroyed_;
  }

  // Waits until the bytes written by the transport contain `needle`.
  bool WaitForWritten(absl::string_view needle) {
    MutexLock lock(mu_);
    const absl::Time deadline = absl::Now() + kTimeout;
    while (written_.find(needle) == std::string::npos) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) break;
    }
    return written_.find(needle) != std::string::npos;
  }

  std::string written() {
    MutexLock lock(mu_);
    return written_;
  }

 private:
  Mutex mu_;
  CondVar cv_;
  std::shared_ptr<SendFrameCallback> send_frame_cb_ ABSL_GUARDED_BY(mu_);
  int null_callback_count_ ABSL_GUARDED_BY(mu_) = 0;
  absl::AnyInvocable<void(absl::Status)> on_read_ ABSL_GUARDED_BY(mu_);
  grpc_event_engine::experimental::SliceBuffer* read_buffer_
      ABSL_GUARDED_BY(mu_) = nullptr;
  std::string written_ ABSL_GUARDED_BY(mu_);
  bool endpoint_destroyed_ ABSL_GUARDED_BY(mu_) = false;
};

class FakeFramingExtension : public TransportFramingEndpointExtension {
 public:
  explicit FakeFramingExtension(std::shared_ptr<FakeEndpointState> state)
      : state_(std::move(state)) {}

  void SetSendFrameCallback(
      absl::AnyInvocable<void(SliceBuffer data)> cb) override {
    state_->SetSendFrameCallback(std::move(cb));
  }

  void ReceiveFrame(SliceBuffer /*data*/) override {}

 private:
  std::shared_ptr<FakeEndpointState> state_;
};

class FakeEndpoint : public EventEngine::Endpoint {
 public:
  explicit FakeEndpoint(std::shared_ptr<FakeEndpointState> state)
      : state_(state),
        extension_(std::move(state)),
        peer_address_(URIToResolvedAddress("ipv4:127.0.0.1:12345").value()),
        local_address_(URIToResolvedAddress("ipv4:127.0.0.1:23456").value()) {}

  ~FakeEndpoint() override { state_->OnEndpointDestroyed(); }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer,
            ReadArgs /*args*/) override {
    return state_->Read(std::move(on_read), buffer);
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> /*on_writable*/,
             grpc_event_engine::experimental::SliceBuffer* data,
             WriteArgs /*args*/) override {
    return state_->Write(data);
  }

  const EventEngine::ResolvedAddress& GetPeerAddress() const override {
    return peer_address_;
  }
  const EventEngine::ResolvedAddress& GetLocalAddress() const override {
    return local_address_;
  }
  std::shared_ptr<TelemetryInfo> GetTelemetryInfo() const override {
    return nullptr;
  }

  void* QueryExtension(absl::string_view id) override {
    if (id == TransportFramingEndpointExtension::EndpointExtensionName()) {
      return &extension_;
    }
    return nullptr;
  }

 private:
  std::shared_ptr<FakeEndpointState> state_;
  FakeFramingExtension extension_;
  EventEngine::ResolvedAddress peer_address_;
  EventEngine::ResolvedAddress local_address_;
};

class Chttp2SecurityFrameTest : public ::testing::Test {
 protected:
  Chttp2SecurityFrameTest() {
    GRPC_CLOSURE_INIT(&on_close_, OnClose, this, nullptr);
    ExecCtx exec_ctx;
    ChannelArgs args = ChannelArgs()
                           .SetObject(ResourceQuota::Default())
                           .SetObject(GetDefaultEventEngine())
                           .Set(GRPC_ARG_SECURITY_FRAME_ALLOWED, true);
    grpc_endpoint* ep =
        grpc_event_engine::experimental::grpc_event_engine_endpoint_create(
            std::make_unique<FakeEndpoint>(state_));
    transport_ = grpc_create_chttp2_transport(
        args, OrphanablePtr<grpc_endpoint>(ep), /*is_client=*/true);
    grpc_chttp2_transport_start_reading(transport_, nullptr, nullptr, nullptr,
                                        &on_close_);
  }

  ~Chttp2SecurityFrameTest() override {
    if (transport_ != nullptr) {
      ExecCtx exec_ctx;
      transport_->Orphan();
    }
    // Wait for the transport to release the endpoint so all asynchronous
    // teardown completes within the test. Leaks are caught by ASAN/LSAN.
    EXPECT_TRUE(state_->WaitForEndpointDestroyed());
    EXPECT_TRUE(close_notification_.WaitForNotificationWithTimeout(kTimeout));
  }

  static void OnClose(void* arg, grpc_error_handle error) {
    auto* self = static_cast<Chttp2SecurityFrameTest*>(arg);
    self->close_error_ = error;
    self->close_notification_.Notify();
  }

  std::shared_ptr<FakeEndpointState> state_ =
      std::make_shared<FakeEndpointState>();
  Transport* transport_ = nullptr;
  grpc_closure on_close_;
  absl::Notification close_notification_;
  grpc_error_handle close_error_;
};

// A SECURITY frame requested before the peer's initial SETTINGS must not close
// the transport. It is buffered (latest frame only) and written once SETTINGS
// advertising GRPC_ALLOW_SECURITY_FRAME are parsed.
TEST_F(Chttp2SecurityFrameTest,
       FrameBeforePeerSettingsIsBufferedUntilSettings) {
  ASSERT_TRUE(state_->has_send_frame_callback());
  state_->SendFrame("frame1");
  state_->SendFrame("frame2");

  EXPECT_FALSE(close_notification_.HasBeenNotified());
  EXPECT_TRUE(state_->has_send_frame_callback());
  EXPECT_EQ(state_->written().find("frame"), std::string::npos);

  state_->DeliverRead(kSettingsAllowSecurityFrame);
  ASSERT_TRUE(state_->WaitForWritten(SecurityFrame("frame2")));
  EXPECT_EQ(state_->written().find("frame1"), std::string::npos);
  EXPECT_FALSE(close_notification_.HasBeenNotified());
  EXPECT_TRUE(state_->has_send_frame_callback());

  // Once SETTINGS are applied, later frames are written directly.
  state_->SendFrame("frame3");
  EXPECT_TRUE(state_->WaitForWritten(SecurityFrame("frame3")));
}

// If the peer's SETTINGS do not allow SECURITY frames, a buffered frame closes
// the transport with FailedPrecondition when the SETTINGS are applied.
TEST_F(Chttp2SecurityFrameTest,
       BufferedFrameClosesTransportWhenPeerDisallowsSecurityFrames) {
  state_->SendFrame("frame1");
  state_->DeliverRead(kEmptySettings);

  ASSERT_TRUE(close_notification_.WaitForNotificationWithTimeout(kTimeout));
  EXPECT_FALSE(close_error_.ok());
  EXPECT_THAT(
      StatusToString(close_error_),
      ::testing::HasSubstr("Unexpected SECURITY frame scheduled for write"));
  EXPECT_TRUE(state_->WaitForNullCallback());
  EXPECT_FALSE(state_->has_send_frame_callback());
  EXPECT_EQ(state_->written().find("frame1"), std::string::npos);
}

// Closing the transport unregisters the send-frame callback (releasing the
// transport ref it holds) and discards any buffered frame. ASAN/LSAN verifies
// that the transport is not leaked through the callback's ref.
TEST_F(Chttp2SecurityFrameTest, TransportCloseUnregistersSendFrameCallback) {
  ASSERT_TRUE(state_->has_send_frame_callback());
  state_->SendFrame("frame1");
  {
    ExecCtx exec_ctx;
    std::exchange(transport_, nullptr)->Orphan();
  }
  EXPECT_TRUE(state_->WaitForNullCallback());
  EXPECT_FALSE(state_->has_send_frame_callback());
  EXPECT_TRUE(state_->WaitForEndpointDestroyed());
  EXPECT_EQ(state_->written().find("frame1"), std::string::npos);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
