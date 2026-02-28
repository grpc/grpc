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

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/slice.h>
#include <grpc/grpc.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/security_frame.h"
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/inter_activity_latch.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/transport_framing_endpoint_extension.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace http2 {

namespace testing {

using ::grpc_event_engine::experimental::EndpointConfig;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::MemoryAllocatorFactory;

class MockTransportFramingEndpointExtension
    : public TransportFramingEndpointExtension {
 public:
  void SetSendFrameCallback(
      absl::AnyInvocable<void(SliceBuffer*)> send_frame_callback) override {
    LOG(INFO) << "MockTransportFramingEndpointExtension::SetSendFrameCallback";
    send_frame_callback_ = std::move(send_frame_callback);
  }

  void TriggerSendFrameCallback(SliceBuffer* data) {
    LOG(INFO)
        << "MockTransportFramingEndpointExtension::TriggerSendFrameCallback";
    GRPC_CHECK(send_frame_callback_ != nullptr);
    send_frame_callback_(data);
  }

  void ReceiveFrame(SliceBuffer payload) override {
    LOG(INFO) << "MockTransportFramingEndpointExtension::ReceiveFrame";
    last_received_payload_.Clear();
    last_received_payload_.Swap(&payload);
  }

  absl::AnyInvocable<void(SliceBuffer*)> send_frame_callback_ = nullptr;
  SliceBuffer last_received_payload_;
};

// We need this class only to get QueryExtension working from inside
// SecurityFrameHandler::Initialize()
// This is a simple wrapper class for EventEngine which just has some additional
// logic for QueryExtension function.
class ExtensionInjectingEventEngine : public EventEngine {
 public:
  explicit ExtensionInjectingEventEngine(
      MockTransportFramingEndpointExtension* extension)
      : wrapped_(GetDefaultEventEngine()), extension_(extension) {}

  void* QueryExtension(absl::string_view id) override {
    if (id == TransportFramingEndpointExtension::EndpointExtensionName()) {
      return extension_;
    }
    return wrapped_->QueryExtension(id);
  }

  absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
      Listener::AcceptCallback on_accept,
      absl::AnyInvocable<void(absl::Status)> on_shutdown,
      const EndpointConfig& config,
      std::unique_ptr<MemoryAllocatorFactory> memory_allocator_factory)
      override {
    return wrapped_->CreateListener(std::move(on_accept),
                                    std::move(on_shutdown), config,
                                    std::move(memory_allocator_factory));
  }

  ConnectionHandle Connect(
      OnConnectCallback on_connect, const ResolvedAddress& addr,
      const EndpointConfig& args, MemoryAllocator memory_allocator,
      grpc_event_engine::experimental::EventEngine::Duration timeout) override {
    return wrapped_->Connect(std::move(on_connect), addr, args,
                             std::move(memory_allocator), timeout);
  }

  bool CancelConnect(ConnectionHandle handle) override {
    return wrapped_->CancelConnect(handle);
  }

  bool IsWorkerThread() override { return wrapped_->IsWorkerThread(); }

  absl::StatusOr<std::unique_ptr<DNSResolver>> GetDNSResolver(
      const DNSResolver::ResolverOptions& options) override {
    return wrapped_->GetDNSResolver(options);
  }

  void Run(Closure* closure) override { wrapped_->Run(closure); }
  void Run(absl::AnyInvocable<void()> closure) override {
    wrapped_->Run(std::move(closure));
  }
  TaskHandle RunAfter(Duration when, Closure* closure) override {
    return wrapped_->RunAfter(when, closure);
  }
  TaskHandle RunAfter(Duration when,
                      absl::AnyInvocable<void()> closure) override {
    return wrapped_->RunAfter(when, std::move(closure));
  }
  bool Cancel(TaskHandle handle) override { return wrapped_->Cancel(handle); }

  std::shared_ptr<EventEngine> wrapped_;

 private:
  MockTransportFramingEndpointExtension* extension_;
};

class SimulatedTransport : public RefCounted<SimulatedTransport> {
 public:
  SimulatedTransport()
      : security_frame_handler_(MakeRefCounted<SecurityFrameHandler>()) {
    event_engine_ =
        std::make_shared<ExtensionInjectingEventEngine>(&mock_extension_);
    EXPECT_TRUE(security_frame_handler_->Initialize(event_engine_).is_set);
    transport_write_context_.StartWriteCycle();
    // Discard the connection preface
    MaybeFlushWriteBuffer();
  }
  void OnTransportClosed() {
    LOG(INFO) << "SimulatedTransport::OnTransportClosed";
    security_frame_handler_->OnTransportClosed();
  }

  ~SimulatedTransport() override {
    LOG(INFO) << "SimulatedTransport::~SimulatedTransport";
    OnTransportClosed();
  }

  auto SecurityFrameLoop() {
    return Loop([self = Ref()]() {
      LOG(INFO) << "SecurityFrameLoop: Loop";
      return Map(
          self->security_frame_handler_->WaitForSecurityFrameSending(),
          [self](Empty) -> LoopCtl<Empty> {
            if (self->security_frame_handler_->TriggerWriteSecurityFrame()
                    .terminate) {
              LOG(INFO) << "SecurityFrameLoop: No security frame to write, "
                           "ending loop.";
              return Empty{};
            }
            self->waker_.Wakeup();
            return Continue{};
          });
    });
  }

  void MaybeAppendSecurityFrame() {
    LOG(INFO) << "SimulatedTransport::MaybeAppendSecurityFrame";
    const uint32_t previous_length = output_buffer.Length();
    WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
    http2::FrameSender frame_sender = write_cycle.GetFrameSender();
    security_frame_handler_->MaybeAppendSecurityFrame(frame_sender);
    if (write_cycle.CanSerializeRegularFrames()) {
      bool unused = false;
      SliceBuffer serialized = write_cycle.SerializeRegularFrames({unused});
      output_buffer.Append(serialized);
    }
    EXPECT_GE(output_buffer.Length(), previous_length);
  }

  void ProcessHttp2SecurityFrame(SliceBuffer&& payload) {
    LOG(INFO) << "SimulatedTransport::ProcessHttp2SecurityFrame";
    security_frame_handler_->ProcessPayload(std::move(payload));
  }

  void MaybeFlushWriteBuffer() {
    WriteCycle& write_cycle = transport_write_context_.GetWriteCycle();
    if (write_cycle.CanSerializeRegularFrames()) {
      bool unused = false;
      SliceBuffer serialized = write_cycle.SerializeRegularFrames({unused});
    }
  }

  SliceBuffer output_buffer;
  MockTransportFramingEndpointExtension mock_extension_;
  RefCountedPtr<SecurityFrameHandler> security_frame_handler_;
  std::shared_ptr<EventEngine> event_engine_;
  Waker waker_;
  TransportWriteContext transport_write_context_{/*is_client=*/true};
};

}  // namespace testing

class SecurityFrameHandlerTest : public ::testing::Test {
 public:
  SecurityFrameHandlerTest() {
    transport_write_context_.StartWriteCycle();

    // Flush connection preface
    MaybeFlushWriteBuffer();
  }

 protected:
  RefCountedPtr<Party> MakeParty(testing::SimulatedTransport* transport) {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        transport->event_engine_.get());
    return Party::Make(std::move(arena));
  }

  WriteCycle& GetWriteCycle() {
    return transport_write_context_.GetWriteCycle();
  }

 private:
  void MaybeFlushWriteBuffer() {
    if (transport_write_context_.GetWriteCycle().CanSerializeRegularFrames()) {
      bool unused = false;
      SliceBuffer serialized =
          transport_write_context_.GetWriteCycle().SerializeRegularFrames(
              {unused});
    }
  }
  TransportWriteContext transport_write_context_{/*is_client=*/true};
};

TEST_F(SecurityFrameHandlerTest, SendFrameCallbackFactoryTest) {
  auto transport = MakeRefCounted<testing::SimulatedTransport>();
  auto callback = transport->security_frame_handler_->SendFrameCallbackFactory(
      transport->event_engine_);

  Notification n;
  // Verify that it triggers the sending mechanism
  RefCountedPtr<Party> party = MakeParty(transport.get());
  party->Spawn(
      "VerifySending",
      Seq(transport->security_frame_handler_->WaitForSecurityFrameSending(),
          []() -> absl::Status { return absl::OkStatus(); }),
      [&n](absl::Status status) { n.Notify(); });

  SliceBuffer payload;
  payload.Append(Slice::FromCopiedString("test_data"));

  // Run the callback
  callback(&payload);

  n.WaitForNotification();
  // Verify payload is received
  EXPECT_THAT(transport->security_frame_handler_->TestOnlyDebugString(),
              ::testing::HasSubstr("payload_length=9"));
}

TEST_F(SecurityFrameHandlerTest, ProcessPayloadTest) {
  ExecCtx exec_ctx;
  auto transport = MakeRefCounted<testing::SimulatedTransport>();
  SliceBuffer payload1;
  payload1.Append(Slice::FromCopiedString("Hello"));
  transport->ProcessHttp2SecurityFrame(std::move(payload1));
  EXPECT_EQ(transport->mock_extension_.last_received_payload_.JoinIntoString(),
            "Hello");

  SliceBuffer payload2;
  payload2.Append(Slice::FromCopiedString("World"));
  transport->ProcessHttp2SecurityFrame(std::move(payload2));
  EXPECT_EQ(transport->mock_extension_.last_received_payload_.JoinIntoString(),
            "World");

  SliceBuffer payload3;
  payload3.Append(Slice::FromCopiedString("Hello"));
  transport->OnTransportClosed();
  transport->ProcessHttp2SecurityFrame(std::move(payload3));
  EXPECT_EQ(transport->mock_extension_.last_received_payload_.JoinIntoString(),
            "World");
}

TEST_F(SecurityFrameHandlerTest, OnTransportClosedPreventsSending) {
  ExecCtx exec_ctx;
  RefCountedPtr<testing::SimulatedTransport> transport =
      MakeRefCounted<testing::SimulatedTransport>();
  EXPECT_THAT(transport->security_frame_handler_->TestOnlyDebugString(),
              ::testing::HasSubstr("payload_length=0"));
  EXPECT_THAT(transport->security_frame_handler_->TestOnlyDebugString(),
              ::testing::HasSubstr("transport_closed_=false"));
  transport->OnTransportClosed();
  SliceBuffer payload;
  payload.Append(Slice::FromCopiedString("hello"));
  transport->mock_extension_.TriggerSendFrameCallback(&payload);
  // Give event engine time to run
  absl::SleepFor(absl::Seconds(1));
  EXPECT_THAT(transport->security_frame_handler_->TestOnlyDebugString(),
              ::testing::HasSubstr("payload_length=0"));
  EXPECT_THAT(transport->security_frame_handler_->TestOnlyDebugString(),
              ::testing::HasSubstr("transport_closed_=true"));
}

TEST_F(SecurityFrameHandlerTest,
       MaybeAppendSecurityFrameDoesNothingIfNotScheduled) {
  ExecCtx exec_ctx;
  auto transport = MakeRefCounted<testing::SimulatedTransport>();
  transport->output_buffer.Append(Slice::FromCopiedString("existing"));
  EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
            SecurityFrameHandler::SleepState::kWaitingForFrame);
  // Must be a NOOP if in kWaitingForFrame state.
  transport->MaybeAppendSecurityFrame();
  EXPECT_EQ(transport->output_buffer.JoinIntoString(), "existing");
  EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
            SecurityFrameHandler::SleepState::kWaitingForFrame);
  transport->OnTransportClosed();
  // Must be a NOOP if transport is closed.
  transport->MaybeAppendSecurityFrame();
  EXPECT_EQ(transport->output_buffer.JoinIntoString(), "existing");
  EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
            SecurityFrameHandler::SleepState::kTransportClosed);
}

TEST_F(SecurityFrameHandlerTest, ExtensionNullTest) {
  // Check if member functions of SecurityFrameHandler are safe to call when
  // endpoint_extension_ is null.
  auto handler = MakeRefCounted<SecurityFrameHandler>();
  auto event_engine =
      std::make_shared<testing::ExtensionInjectingEventEngine>(nullptr);
  EXPECT_FALSE(handler->Initialize(event_engine).is_set);

  EXPECT_THAT(handler->TestOnlyDebugString(),
              ::testing::HasSubstr("endpoint_extension_=null"));
  EXPECT_EQ(handler->TestOnlySleepState(),
            SecurityFrameHandler::SleepState::kWaitingForFrame);

  SliceBuffer payload;
  payload.Append(Slice::FromCopiedString("test"));
  handler->ProcessPayload(std::move(payload));

  SliceBuffer outbuf;
  http2::FrameSender frame_sender = GetWriteCycle().GetFrameSender();
  handler->MaybeAppendSecurityFrame(frame_sender);
  if (GetWriteCycle().CanSerializeRegularFrames()) {
    bool should_reset = false;
    outbuf.Append(GetWriteCycle().SerializeRegularFrames({should_reset}));
  }
  EXPECT_EQ(outbuf.Length(), 0);

  handler->OnTransportClosed();
}

TEST_F(SecurityFrameHandlerTest, MaybeAppendSecurityFrameWithPayload) {
  auto transport = MakeRefCounted<testing::SimulatedTransport>();
  transport->output_buffer.Append(Slice::FromCopiedString("existing"));
  EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
            SecurityFrameHandler::SleepState::kWaitingForFrame);

  RefCountedPtr<Party> party = MakeParty(transport.get());
  Notification n;
  party->Spawn(
      "AppendFrame",
      Seq(transport->security_frame_handler_->WaitForSecurityFrameSending(),
          [&transport]() {
            EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
                      SecurityFrameHandler::SleepState::kWriteOneFrame);
            EXPECT_THAT(
                transport->security_frame_handler_->TestOnlyDebugString(),
                ::testing::HasSubstr("payload_length=5"));
            EXPECT_FALSE(
                transport->security_frame_handler_->TriggerWriteSecurityFrame()
                    .terminate);
            EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
                      SecurityFrameHandler::SleepState::kScheduledWrite);
            EXPECT_THAT(
                transport->security_frame_handler_->TestOnlyDebugString(),
                ::testing::HasSubstr("payload_length=5"));
            transport->MaybeAppendSecurityFrame();
            EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
                      SecurityFrameHandler::SleepState::kWaitingForFrame);
            EXPECT_THAT(
                transport->security_frame_handler_->TestOnlyDebugString(),
                ::testing::HasSubstr("payload_length=0"));

            // Check frame was appended.
            // A security frame has 9 bytes header:
            // 3 bytes length, 1 byte type (200), 1 byte flags (0), 4 bytes
            // stream_id (0) length should be 5 for "Hello".
            // 00 00 05 C8 00 00 00 00 00
            // The frame is serialized to output buffer.
            EXPECT_EQ(transport->output_buffer.Length(),
                      std::string("existing").length() + 9 + 5);
            SliceBuffer prefix;
            transport->output_buffer.MoveFirstNBytesIntoSliceBuffer(
                std::string("existing").length(), prefix);
            // Frame header: 5 bytes length, type=200, flags=0, stream=0
            uint8_t header_buf[9];
            transport->output_buffer.MoveFirstNBytesIntoBuffer(9, header_buf);
            Http2FrameHeader header = Http2FrameHeader::Parse(header_buf);
            EXPECT_EQ(header.length, 5);
            EXPECT_EQ(header.type, 200);
            EXPECT_EQ(header.flags, 0);
            EXPECT_EQ(header.stream_id, 0);
            EXPECT_EQ(transport->output_buffer.JoinIntoString(), "Hello");
            return Empty{};
          }),
      [&n](Empty) { n.Notify(); });

  // Send a frame to trigger payload.
  SliceBuffer payload;
  payload.Append(Slice::FromCopiedString("Hello"));
  transport->mock_extension_.TriggerSendFrameCallback(&payload);

  n.WaitForNotification();
}

TEST_F(SecurityFrameHandlerTest, SimulatorTest) {
  RefCountedPtr<testing::SimulatedTransport> transport =
      MakeRefCounted<testing::SimulatedTransport>();
  EXPECT_EQ(transport->security_frame_handler_->TestOnlySleepState(),
            SecurityFrameHandler::SleepState::kWaitingForFrame);
  transport->output_buffer.Append(Slice::FromCopiedString("Init"));

  RefCountedPtr<Party> transport_party = MakeParty(transport.get());
  RefCountedPtr<Party> other_party = MakeParty(transport.get());
  InterActivityLatch<void> write_to_endpoint_key;
  Notification n1;

  LOG(INFO) << "SimulatorTest: Spawning SecurityFrameLoop";
  transport_party->Spawn("SecurityFrameLoop", transport->SecurityFrameLoop(),
                         [&n1](auto) {
                           LOG(INFO) << "SecurityFrameLoop: finished";
                           n1.Notify();
                         });

  LOG(INFO) << "SimulatorTest: Spawning TransportClose";
  transport_party->Spawn(
      "Transport",
      [&write_to_endpoint_key,
       self = transport->RefAsSubclass<testing::SimulatedTransport>()]() {
        EXPECT_EQ(self->security_frame_handler_->TestOnlySleepState(),
                  SecurityFrameHandler::SleepState::kWaitingForFrame);
        return TrySeq(
            [self]() -> Poll<Empty> {
              LOG(INFO) << "TransportClose: polling for kScheduledWrite";
              // Wait for SecurityFrameLoop
              if (self->security_frame_handler_->TestOnlySleepState() !=
                  SecurityFrameHandler::SleepState::kScheduledWrite) {
                LOG(INFO) << "TransportClose: waiting for kScheduledWrite";
                self->waker_ = GetContext<Activity>()->MakeNonOwningWaker();
                return Pending{};
              }
              LOG(INFO) << "TransportClose: got kScheduledWrite";
              return Empty{};
            },
            [&write_to_endpoint_key, self]() {
              LOG(INFO) << "Transport: MaybeAppendSecurityFrame";
              EXPECT_EQ(self->security_frame_handler_->TestOnlySleepState(),
                        SecurityFrameHandler::SleepState::kScheduledWrite);
              self->MaybeAppendSecurityFrame();
              EXPECT_EQ(self->security_frame_handler_->TestOnlySleepState(),
                        SecurityFrameHandler::SleepState::kWaitingForFrame);
              EXPECT_EQ(self->output_buffer.Length(), 4 + 9 + 5);
              self->output_buffer.Clear();
              write_to_endpoint_key.Set();
              LOG(INFO) << "Transport: MaybeAppendSecurityFrame Done";
            },
            [self]() -> Poll<Empty> {
              // Wait for SecurityFrameLoop
              if (self->security_frame_handler_->TestOnlySleepState() !=
                  SecurityFrameHandler::SleepState::kScheduledWrite) {
                self->waker_ = GetContext<Activity>()->MakeNonOwningWaker();
                return Pending{};
              }
              return Empty{};
            },
            [self]() {
              EXPECT_EQ(self->security_frame_handler_->TestOnlySleepState(),
                        SecurityFrameHandler::SleepState::kScheduledWrite);
              self->MaybeAppendSecurityFrame();
              EXPECT_EQ(self->security_frame_handler_->TestOnlySleepState(),
                        SecurityFrameHandler::SleepState::kWaitingForFrame);
              EXPECT_EQ(self->output_buffer.Length(), 9 + 5);
              self->output_buffer.Clear();
            },
            [self]() {
              LOG(INFO) << "Transport: Closing";
              self->OnTransportClosed();
              return Empty{};
            });
      },
      [](auto) { LOG(INFO) << "Transport: finished"; });
  LOG(INFO) << "SimulatorTest: Spawning SendSecurityFrame";
  other_party->Spawn(
      "SendSecurityFrame",
      [&write_to_endpoint_key,
       self = transport->RefAsSubclass<testing::SimulatedTransport>()]() {
        LOG(INFO) << "SendSecurityFrame: spawned";
        return TrySeq(
            [self]() -> Empty {
              SliceBuffer payload;
              payload.Append(Slice::FromCopiedString("Hello"));
              LOG(INFO) << "OtherParty: TriggerSendFrameCallback with 'Hello'";
              self->mock_extension_.TriggerSendFrameCallback(&payload);
              return Empty{};
            },
            [&write_to_endpoint_key]() {
              LOG(INFO) << "OtherParty: write_to_endpoint_key.Wait";
              return write_to_endpoint_key.Wait();
            },
            [self]() -> Empty {
              SliceBuffer payload;
              payload.Append(Slice::FromCopiedString("world"));
              LOG(INFO) << "OtherParty: TriggerSendFrameCallback with 'world'";
              self->mock_extension_.TriggerSendFrameCallback(&payload);
              return Empty{};
            });
      },
      [](auto) { LOG(INFO) << "SendSecurityFrame: finished"; });
  LOG(INFO) << "SimulatorTest: Waiting for SecurityFrameLoop to finish";
  n1.WaitForNotification();
  LOG(INFO) << "SimulatorTest: End";
}

}  // namespace http2
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // Must call to create default EventEngine.
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
