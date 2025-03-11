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

#include "src/core/lib/surface/client_call.h"

#include <grpc/compression.h>
#include <grpc/grpc.h>

#include "absl/status/status.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/metadata.h"
#include "src/core/util/debug_location.h"
#include "test/core/call/batch_builder.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

namespace {
const absl::string_view kDefaultPath = "/foo/bar";
}

class ClientCallTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  class CallOptions {
   public:
    Slice path() const { return path_.Copy(); }
    std::optional<Slice> authority() const {
      return authority_.has_value() ? std::optional<Slice>(authority_->Copy())
                                    : std::nullopt;
    }
    bool registered_method() const { return registered_method_; }
    Duration timeout() const { return timeout_; }
    grpc_compression_options compression_options() const {
      return compression_options_;
    }

    CallOptions& SetTimeout(Duration timeout) {
      timeout_ = timeout;
      return *this;
    }

   private:
    Slice path_ = Slice::FromCopiedString(kDefaultPath);
    std::optional<Slice> authority_;
    bool registered_method_ = false;
    Duration timeout_ = Duration::Infinity();
    grpc_compression_options compression_options_ = {
        1,
        {0, GRPC_COMPRESS_LEVEL_NONE},
        {0, GRPC_COMPRESS_NONE},
    };
  };

  grpc_call* InitCall(const CallOptions& options) {
    CHECK_EQ(call_, nullptr);
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    call_ = MakeClientCall(
        nullptr, 0, cq_, options.path(), options.authority(),
        options.registered_method(), options.timeout() + Timestamp::Now(),
        options.compression_options(), std::move(arena), destination_);
    return call_;
  }

  BatchBuilder NewBatch(int tag) {
    return BatchBuilder(call_, cq_verifier_.get(), tag);
  }

  // Pull in CqVerifier types for ergonomics
  using ExpectedResult = CqVerifier::ExpectedResult;
  using Maybe = CqVerifier::Maybe;
  using PerformAction = CqVerifier::PerformAction;
  using MaybePerformAction = CqVerifier::MaybePerformAction;
  using AnyStatus = CqVerifier::AnyStatus;
  void Expect(int tag, ExpectedResult result, SourceLocation whence = {}) {
    expectations_++;
    cq_verifier_->Expect(CqVerifier::tag(tag), std::move(result), whence);
  }

  void TickThroughCqExpectations(std::optional<Duration> timeout = std::nullopt,
                                 SourceLocation whence = {}) {
    if (expectations_ == 0) {
      cq_verifier_->VerifyEmpty(timeout.value_or(Duration::Seconds(1)), whence);
      return;
    }
    expectations_ = 0;
    cq_verifier_->Verify(timeout.value_or(Duration::Minutes(5)), whence);
  }

  CallHandler& handler() {
    CHECK(handler_.has_value());
    return *handler_;
  }

 private:
  class TestCallDestination final : public UnstartedCallDestination {
   public:
    explicit TestCallDestination(ClientCallTest* test) : test_(test) {}

    void Orphaned() override {}
    void StartCall(UnstartedCallHandler handler) override {
      CHECK(!test_->handler_.has_value());
      test_->handler_.emplace(handler.StartCall());
    }

   private:
    ClientCallTest* const test_;
  };

  void InitTest() override {
    cq_ = grpc_completion_queue_create_for_next(nullptr);
    cq_verifier_ = absl::make_unique<CqVerifier>(
        cq_, CqVerifier::FailUsingGprCrash,
        [this](
            grpc_event_engine::experimental::EventEngine::Duration max_step) {
          event_engine()->Tick(max_step);
        });
  }

  void Shutdown() override {
    if (call_ != nullptr) {
      grpc_call_unref(call_);
    }
    handler_.reset();
    grpc_completion_queue_shutdown(cq_);
    auto ev = grpc_completion_queue_next(
        cq_, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    CHECK_EQ(ev.type, GRPC_QUEUE_SHUTDOWN);
    grpc_completion_queue_destroy(cq_);
  }

  grpc_completion_queue* cq_ = nullptr;
  grpc_call* call_ = nullptr;
  RefCountedPtr<TestCallDestination> destination_ =
      MakeRefCounted<TestCallDestination>(this);
  std::optional<CallHandler> handler_;
  std::unique_ptr<CqVerifier> cq_verifier_;
  int expectations_ = 0;
};

#define CLIENT_CALL_TEST(name) YODEL_TEST(ClientCallTest, name)

CLIENT_CALL_TEST(NoOp) { InitCall(CallOptions()); }

CLIENT_CALL_TEST(SendInitialMetadata) {
  InitCall(CallOptions());
  NewBatch(1).SendInitialMetadata({
      {"foo", "bar"},
  });
  Expect(1, true);
  TickThroughCqExpectations();
  SpawnTestSeq(
      handler(), "pull-initial-metadata",
      [this]() { return handler().PullClientInitialMetadata(); },
      [](ValueOrFailure<ClientMetadataHandle> md) {
        CHECK(md.ok());
        CHECK_NE((*md)->get_pointer(HttpPathMetadata()), nullptr);
        EXPECT_EQ((*md)->get_pointer(HttpPathMetadata())->as_string_view(),
                  kDefaultPath);
        std::string buffer;
        auto r = (*md)->GetStringValue("foo", &buffer);
        EXPECT_EQ(r, "bar");
        return Immediate(Empty{});
      });
  WaitForAllPendingWork();
}

CLIENT_CALL_TEST(SendInitialMetadataAndReceiveStatusAfterCancellation) {
  InitCall(CallOptions());
  IncomingStatusOnClient status;
  NewBatch(1).SendInitialMetadata({}).RecvStatusOnClient(status);
  SpawnTestSeq(
      handler(), "pull-initial-metadata",
      [this]() { return handler().PullClientInitialMetadata(); },
      [this](ValueOrFailure<ClientMetadataHandle> md) {
        CHECK(md.ok());
        EXPECT_EQ((*md)->get_pointer(HttpPathMetadata())->as_string_view(),
                  kDefaultPath);
        handler().PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_INTERNAL, "test error"));
        return Immediate(Empty{});
      });
  Expect(1, true);
  TickThroughCqExpectations();
  EXPECT_EQ(status.status(), GRPC_STATUS_INTERNAL);
  EXPECT_EQ(status.message(), "test error");
  WaitForAllPendingWork();
}

CLIENT_CALL_TEST(SendInitialMetadataAndReceiveStatusAfterTimeout) {
  auto start = Timestamp::Now();
  InitCall(CallOptions().SetTimeout(Duration::Seconds(1)));
  IncomingStatusOnClient status;
  NewBatch(1).SendInitialMetadata({}).RecvStatusOnClient(status);
  Expect(1, true);
  TickThroughCqExpectations();
  EXPECT_EQ(status.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
  ExecCtx::Get()->InvalidateNow();
  auto now = Timestamp::Now();
  EXPECT_GE(now - start, Duration::Seconds(1)) << GRPC_DUMP_ARGS(now, start);
  EXPECT_LE(now - start, Duration::Minutes(10)) << GRPC_DUMP_ARGS(now, start);
  WaitForAllPendingWork();
}

CLIENT_CALL_TEST(CancelBeforeInvoke1) {
  grpc_call_cancel(InitCall(CallOptions()), nullptr);
  IncomingStatusOnClient status;
  NewBatch(1).RecvStatusOnClient(status);
  Expect(1, true);
  TickThroughCqExpectations();
  EXPECT_EQ(status.status(), GRPC_STATUS_CANCELLED);
}

CLIENT_CALL_TEST(CancelBeforeInvoke2) {
  grpc_call_cancel(InitCall(CallOptions()), nullptr);
  IncomingStatusOnClient status;
  NewBatch(1).RecvStatusOnClient(status).SendInitialMetadata({});
  Expect(1, true);
  TickThroughCqExpectations();
  EXPECT_EQ(status.status(), GRPC_STATUS_CANCELLED);
}

CLIENT_CALL_TEST(NegativeDeadline) {
  auto start = Timestamp::Now();
  InitCall(CallOptions().SetTimeout(Duration::Seconds(-1)));
  IncomingStatusOnClient status;
  NewBatch(1).SendInitialMetadata({}).RecvStatusOnClient(status);
  Expect(1, true);
  TickThroughCqExpectations();
  EXPECT_EQ(status.status(), GRPC_STATUS_DEADLINE_EXCEEDED);
  auto now = Timestamp::Now();
  EXPECT_LE(now - start, Duration::Milliseconds(100))
      << GRPC_DUMP_ARGS(now, start);
  WaitForAllPendingWork();
}

TEST(ClientCallTest, NoOpRegression1) {
  NoOp(ParseTestProto(
      R"pb(event_engine_actions {
             assign_ports: 4294967285
             connections { write_size: 1 write_size: 0 write_size: 2147483647 }
           }
      )pb"));
}

}  // namespace grpc_core
