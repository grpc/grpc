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

#include "src/core/xds/xds_client/serialized_streaming_call.h"

#include <grpc/grpc.h>

#include <atomic>
#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/sync.h"
#include "src/core/util/wait_for_single_owner.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/test_util/test_config.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gtest/gtest.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace testing {

struct CallbackEvent {
  enum class Type { kRecvMessage, kStatusReceived };
  Type type;
  std::string payload;
  absl::Status status;

  std::string ToString() const {
    switch (type) {
      case Type::kRecvMessage:
        return absl::StrCat("OnRecvMessage(payload=\"", payload, "\")");
      case Type::kStatusReceived:
        return absl::StrCat("OnStatusReceived(status=", status.ToString(), ")");
    }
  }
};

class FakeEventHandler
    : public XdsTransportFactory::XdsTransport::StreamingCall::EventHandler {
 public:
  explicit FakeEventHandler(
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine)
      : event_engine_(std::move(event_engine)) {}

  void OnRequestSent(bool /*ok*/) override {}

  void OnRecvMessage(absl::string_view payload) override {
    MutexLock lock(&mu_);
    events_.push_back({CallbackEvent::Type::kRecvMessage, std::string(payload),
                       absl::OkStatus()});
  }

  void OnStatusReceived(absl::Status status) override {
    MutexLock lock(&mu_);
    events_.push_back(
        {CallbackEvent::Type::kStatusReceived, "", std::move(status)});
  }

  std::optional<CallbackEvent> WaitForNextEvent() {
    while (true) {
      {
        MutexLock lock(&mu_);
        if (!events_.empty()) {
          CallbackEvent event = std::move(events_.front());
          events_.pop_front();
          return event;
        }
        if (event_engine_->IsIdle()) return std::nullopt;
      }
      event_engine_->Tick();
    }
  }

  bool HasEvent() {
    MutexLock lock(&mu_);
    return !events_.empty();
  }

  void ExpectNoEvent() {
    event_engine_->TickUntilIdle();
    EXPECT_FALSE(HasEvent());
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  Mutex mu_;
  std::deque<CallbackEvent> events_ ABSL_GUARDED_BY(&mu_);
};

class FakeXdsServerTarget : public XdsBootstrap::XdsServerTarget {
 public:
  explicit FakeXdsServerTarget(std::string server_uri)
      : server_uri_(std::move(server_uri)) {}
  const std::string& server_uri() const override { return server_uri_; }
  std::string Key() const override { return server_uri_; }
  bool Equals(const XdsServerTarget& other) const override {
    return server_uri_ == other.server_uri();
  }

 private:
  std::string server_uri_;
};

class SerializedStreamingCallTest : public ::testing::Test {
 protected:
  SerializedStreamingCallTest() : server_target_("localhost:4321") {}

  void SetUp() override {
    event_engine_ =
        std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
            grpc_event_engine::experimental::FuzzingEventEngine::Options(),
            ::fuzzing_event_engine::Actions());
    grpc_timer_manager_set_start_threaded(false);
    grpc_init();
  }

  void TearDown() override {
    transport_.reset();
    transport_factory_.reset();
    event_engine_->FuzzingDone();
    event_engine_->TickUntilIdle();
    event_engine_->UnsetGlobalHooks();
    WaitForSingleOwner(std::move(event_engine_));
    grpc_shutdown_blocking();
  }

  void InitTransport(bool autocomplete = true) {
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        []() { FAIL() << "Multiple concurrent reads"; }, event_engine_);
    transport_factory_->SetAutoCompleteMessagesFromClient(autocomplete);
  }

  OrphanablePtr<SerializedStreamingCall> MakeSerializedCall() {
    absl::Status status;
    transport_ = transport_factory_->GetTransport(server_target_, &status);
    EXPECT_TRUE(status.ok()) << status.ToString();
    auto handler = std::make_unique<FakeEventHandler>(event_engine_);
    event_handler_ = handler.get();
    return MakeOrphanable<SerializedStreamingCall>(
        transport_, FakeXdsTransportFactory::kAdsMethod, std::move(handler));
  }

  RefCountedPtr<Party> MakeParty() {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return Party::Make(std::move(arena));
  }

  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<XdsTransportFactory::XdsTransport> transport_;
  FakeXdsServerTarget server_target_;
  FakeEventHandler* event_handler_ = nullptr;
};

TEST_F(SerializedStreamingCallTest, SingleWriteSuccess) {
  InitTransport(true);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved = false;
  absl::Status status;
  party->Spawn(
      "SendPromise",
      [wrapper = wrapper.get()]() { return wrapper->Send("hello"); },
      [&resolved, &status](absl::Status s) {
        resolved = true;
        status = std::move(s);
      });
  auto msg = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg.has_value());
  EXPECT_EQ(*msg, "hello");
  EXPECT_FALSE(resolved);
  event_engine_->TickUntilIdle();
  EXPECT_TRUE(resolved);
  EXPECT_TRUE(status.ok());
}

TEST_F(SerializedStreamingCallTest, SequentialWrites) {
  InitTransport(true);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  for (int i = 0; i < 5; ++i) {
    std::string payload = absl::StrCat("msg_", i);
    bool resolved = false;
    absl::Status status;
    party->Spawn(
        "SendPromise",
        [wrapper = wrapper.get(), payload]() { return wrapper->Send(payload); },
        [&resolved, &status](absl::Status s) {
          resolved = true;
          status = std::move(s);
        });
    auto msg = fake_stream->WaitForMessageFromClient();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(*msg, payload);
    EXPECT_FALSE(resolved);
    event_engine_->TickUntilIdle();
    EXPECT_TRUE(resolved);
    EXPECT_TRUE(status.ok());
  }
}

TEST_F(SerializedStreamingCallTest, SerializationAndFifo) {
  InitTransport(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved1 = false;
  absl::Status status1;
  bool resolved2 = false;
  absl::Status status2;
  bool resolved3 = false;
  absl::Status status3;
  party->Spawn(
      "Send1", [wrapper = wrapper.get()]() { return wrapper->Send("msg1"); },
      [&resolved1, &status1](absl::Status s) {
        resolved1 = true;
        status1 = std::move(s);
      });
  party->Spawn(
      "Send2", [wrapper = wrapper.get()]() { return wrapper->Send("msg2"); },
      [&resolved2, &status2](absl::Status s) {
        resolved2 = true;
        status2 = std::move(s);
      });
  party->Spawn(
      "Send3", [wrapper = wrapper.get()]() { return wrapper->Send("msg3"); },
      [&resolved3, &status3](absl::Status s) {
        resolved3 = true;
        status3 = std::move(s);
      });
  // Verify only msg1 is forwarded
  auto msg1 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(*msg1, "msg1");
  EXPECT_FALSE(fake_stream->HaveMessageFromClient());
  EXPECT_FALSE(resolved1);
  EXPECT_FALSE(resolved2);
  EXPECT_FALSE(resolved3);
  // Complete msg1
  fake_stream->CompleteSendMessageFromClient(true);
  event_engine_->TickUntilIdle();
  // Verify msg1 is resolved, msg2 is now forwarded
  EXPECT_TRUE(resolved1);
  EXPECT_TRUE(status1.ok());
  EXPECT_FALSE(resolved2);
  EXPECT_FALSE(resolved3);
  auto msg2 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg2.has_value());
  EXPECT_EQ(*msg2, "msg2");
  EXPECT_FALSE(fake_stream->HaveMessageFromClient());
  // Complete msg2
  fake_stream->CompleteSendMessageFromClient(true);
  event_engine_->TickUntilIdle();
  // Verify msg2 is resolved, msg3 is now forwarded
  EXPECT_TRUE(resolved2);
  EXPECT_TRUE(status2.ok());
  EXPECT_FALSE(resolved3);
  auto msg3 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg3.has_value());
  EXPECT_EQ(*msg3, "msg3");
  EXPECT_FALSE(fake_stream->HaveMessageFromClient());
  // Complete msg3
  fake_stream->CompleteSendMessageFromClient(true);
  event_engine_->TickUntilIdle();
  // Verify msg3 is resolved
  EXPECT_TRUE(resolved3);
  EXPECT_TRUE(status3.ok());
}

TEST_F(SerializedStreamingCallTest, SendNonBlocking) {
  InitTransport(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved = false;
  absl::Status status;
  party->Spawn(
      "SendPromise",
      [wrapper = wrapper.get()]() { return wrapper->Send("non_blocking"); },
      [&resolved, &status](absl::Status s) {
        resolved = true;
        status = std::move(s);
      });
  EXPECT_FALSE(resolved);
  auto msg = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg.has_value());
  EXPECT_EQ(*msg, "non_blocking");
}

TEST_F(SerializedStreamingCallTest, WriteFailureQueueClearing) {
  InitTransport(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved1 = false;
  absl::Status status1;
  bool resolved2 = false;
  absl::Status status2;
  party->Spawn(
      "Send1", [wrapper = wrapper.get()]() { return wrapper->Send("msg1"); },
      [&resolved1, &status1](absl::Status s) {
        resolved1 = true;
        status1 = std::move(s);
      });
  party->Spawn(
      "Send2", [wrapper = wrapper.get()]() { return wrapper->Send("msg2"); },
      [&resolved2, &status2](absl::Status s) {
        resolved2 = true;
        status2 = std::move(s);
      });
  // Get msg1 from fake stream
  auto msg1 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(*msg1, "msg1");
  // Complete msg1 with ok = false
  fake_stream->CompleteSendMessageFromClient(false);
  event_engine_->TickUntilIdle();
  // Verify msg1 and msg2 promises both resolve to failure
  EXPECT_TRUE(resolved1);
  EXPECT_FALSE(status1.ok());
  EXPECT_TRUE(resolved2);
  EXPECT_FALSE(status2.ok());
  // Verify msg2 is cleared and not sent
  EXPECT_FALSE(fake_stream->HaveMessageFromClient());
}

TEST_F(SerializedStreamingCallTest, StreamOrphaning) {
  InitTransport(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved1 = false;
  absl::Status status1;
  bool resolved2 = false;
  absl::Status status2;
  party->Spawn(
      "Send1", [wrapper = wrapper.get()]() { return wrapper->Send("msg1"); },
      [&resolved1, &status1](absl::Status s) {
        resolved1 = true;
        status1 = std::move(s);
      });
  party->Spawn(
      "Send2", [wrapper = wrapper.get()]() { return wrapper->Send("msg2"); },
      [&resolved2, &status2](absl::Status s) {
        resolved2 = true;
        status2 = std::move(s);
      });
  auto msg1 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(*msg1, "msg1");
  // Orphan the wrapper
  wrapper.reset();
  // Verify the underlying stream is orphaned
  EXPECT_TRUE(fake_stream->IsOrphaned());
  // Tick the event engine to let cleanup run and resolve pending promises
  event_engine_->TickUntilIdle();
  // Verify all pending promises resolved to failure status
  EXPECT_TRUE(resolved1);
  EXPECT_FALSE(status1.ok());
  EXPECT_TRUE(resolved2);
  EXPECT_FALSE(status2.ok());
}

TEST_F(SerializedStreamingCallTest, RapidSequentialCalls) {
  InitTransport(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  struct Tracker {
    bool resolved = false;
    absl::Status status;
  };
  std::vector<Tracker> trackers(100);
  // Use a separate party for each concurrent send to avoid party-full deadlock
  std::vector<RefCountedPtr<Party>> parties;
  parties.reserve(100);
  // Send 100 messages rapidly
  for (int i = 0; i < 100; ++i) {
    auto party = MakeParty();
    parties.push_back(party);
    party->Spawn(
        absl::StrCat("Send_", i),
        [wrapper = wrapper.get(), i]() {
          return wrapper->Send(absl::StrCat("msg_", i));
        },
        [&tracker = trackers[i]](absl::Status s) {
          tracker.resolved = true;
          tracker.status = std::move(s);
        });
  }
  // Complete them one by one and verify
  for (int i = 0; i < 100; ++i) {
    auto msg = fake_stream->WaitForMessageFromClient();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(*msg, absl::StrCat("msg_", i));
    EXPECT_FALSE(fake_stream->HaveMessageFromClient());
    EXPECT_FALSE(trackers[i].resolved);
    fake_stream->CompleteSendMessageFromClient(true);
    event_engine_->TickUntilIdle();
    EXPECT_TRUE(trackers[i].resolved);
    EXPECT_TRUE(trackers[i].status.ok());
  }
}

TEST_F(SerializedStreamingCallTest, InterleavedReadsAndWrites) {
  InitTransport(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved1 = false;
  absl::Status status1;
  bool resolved2 = false;
  absl::Status status2;
  // Queue some writes
  party->Spawn(
      "Send1", [wrapper = wrapper.get()]() { return wrapper->Send("write_1"); },
      [&resolved1, &status1](absl::Status s) {
        resolved1 = true;
        status1 = std::move(s);
      });
  party->Spawn(
      "Send2", [wrapper = wrapper.get()]() { return wrapper->Send("write_2"); },
      [&resolved2, &status2](absl::Status s) {
        resolved2 = true;
        status2 = std::move(s);
      });
  // Verify only write_1 is forwarded
  auto w1 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(w1.has_value());
  EXPECT_EQ(*w1, "write_1");
  // Start reading from the wrapper
  wrapper->StartRecvMessage();
  // Send a message from the server to the client
  fake_stream->SendMessageToClient("server_response_1");
  // Wait and verify we received the read callback
  auto r_event = event_handler_->WaitForNextEvent();
  ASSERT_TRUE(r_event.has_value());
  EXPECT_EQ(r_event->type, CallbackEvent::Type::kRecvMessage);
  EXPECT_EQ(r_event->payload, "server_response_1");
  // Verify write serialization is unaffected (write_2 is still queued)
  EXPECT_FALSE(fake_stream->HaveMessageFromClient());
  EXPECT_FALSE(resolved1);
  EXPECT_FALSE(resolved2);
  // Complete write_1
  fake_stream->CompleteSendMessageFromClient(true);
  event_engine_->TickUntilIdle();
  // Verify write_1 resolves to OK, write_2 is now forwarded
  EXPECT_TRUE(resolved1);
  EXPECT_TRUE(status1.ok());
  EXPECT_FALSE(resolved2);
  auto w2 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(w2.has_value());
  EXPECT_EQ(*w2, "write_2");
  // Complete write_2
  fake_stream->CompleteSendMessageFromClient(true);
  event_engine_->TickUntilIdle();
  // Verify write_2 resolves to OK
  EXPECT_TRUE(resolved2);
  EXPECT_TRUE(status2.ok());
}

TEST_F(SerializedStreamingCallTest, StreamDestructionCleanup) {
  transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
      []() { FAIL() << "Multiple concurrent reads"; }, event_engine_);
  transport_factory_->SetAbortOnUndrainedMessages(false);
  transport_factory_->SetAutoCompleteMessagesFromClient(false);
  auto wrapper = MakeSerializedCall();
  auto fake_stream = transport_factory_->WaitForStream(
      server_target_, FakeXdsTransportFactory::kAdsMethod);
  ASSERT_NE(fake_stream, nullptr);
  auto party = MakeParty();
  bool resolved1 = false;
  absl::Status status1;
  bool resolved2 = false;
  absl::Status status2;
  bool resolved3 = false;
  absl::Status status3;
  // Send several messages to queue them up
  party->Spawn(
      "Send1", [wrapper = wrapper.get()]() { return wrapper->Send("msg1"); },
      [&resolved1, &status1](absl::Status s) {
        resolved1 = true;
        status1 = std::move(s);
      });
  party->Spawn(
      "Send2", [wrapper = wrapper.get()]() { return wrapper->Send("msg2"); },
      [&resolved2, &status2](absl::Status s) {
        resolved2 = true;
        status2 = std::move(s);
      });
  party->Spawn(
      "Send3", [wrapper = wrapper.get()]() { return wrapper->Send("msg3"); },
      [&resolved3, &status3](absl::Status s) {
        resolved3 = true;
        status3 = std::move(s);
      });
  // Verify only msg1 is forwarded
  auto msg1 = fake_stream->WaitForMessageFromClient();
  ASSERT_TRUE(msg1.has_value());
  EXPECT_EQ(*msg1, "msg1");
  // Destroy the wrapper while msg2 and msg3 are still queued in the wrapper,
  // and msg1 is in-flight.
  wrapper.reset();
  // Verify the underlying stream is orphaned
  EXPECT_TRUE(fake_stream->IsOrphaned());
  // Tick the event engine to let cleanup run and resolve pending promises
  event_engine_->TickUntilIdle();
  // Verify all pending promises resolved to failure status
  EXPECT_TRUE(resolved1);
  EXPECT_FALSE(status1.ok());
  EXPECT_TRUE(resolved2);
  EXPECT_FALSE(status2.ok());
  EXPECT_TRUE(resolved3);
  EXPECT_FALSE(status3.ok());
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}