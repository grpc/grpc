// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TRANSPORT_UTIL_TRANSPORT_TEST_H
#define GRPC_TEST_CORE_TRANSPORT_UTIL_TRANSPORT_TEST_H
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>

#include <cstddef>
#include <deque>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/call_arena_allocator.h"
#include "src/core/call/call_spine.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/promise_endpoint.h"
#include "src/core/util/grpc_check.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/test_util/mock_endpoint.h"
#include "gtest/gtest.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace grpc_core {
namespace util {
namespace testing {

class TransportTest : public ::testing::Test {
 protected:
  const std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>&
  event_engine() const {
    return event_engine_;
  }

  ChannelArgs GetChannelArgs() {
    return CoreConfiguration::Get()
        .channel_args_preconditioning()
        .PreconditionChannelArgs(nullptr)
        .Set(GRPC_ARG_ENABLE_CHANNELZ, true);
  }

  RefCountedPtr<Arena> MakeArena() {
    auto arena = call_arena_allocator_->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    return arena;
  }

  RefCountedPtr<CallArenaAllocator> call_arena_allocator() {
    return call_arena_allocator_;
  }

  auto MakeCall(ClientMetadataHandle client_initial_metadata) {
    return MakeCallPair(std::move(client_initial_metadata), MakeArena());
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_{
          std::make_shared<grpc_event_engine::experimental::FuzzingEventEngine>(
              []() {
                grpc_timer_manager_set_threading(false);
                grpc_event_engine::experimental::FuzzingEventEngine::Options
                    options;
                return options;
              }(),
              fuzzing_event_engine::Actions())};
  RefCountedPtr<CallArenaAllocator> call_arena_allocator_{
      MakeRefCounted<CallArenaAllocator>(
          MakeResourceQuota("test-quota")
              ->memory_quota()
              ->CreateMemoryAllocator("test-allocator"),
          1024)};
};

using EventEngineSlice = grpc_event_engine::experimental::Slice;

// A mock endpoint wrapper that scripts a deterministic sequence of
// Read and Write events for unit testing promise based transports.
class EventSequenceEndpoint
    : public grpc_event_engine::experimental::BaseMockEndpointController,
      public std::enable_shared_from_this<EventSequenceEndpoint> {
 public:
  // A grouping of scripted Read and Write events that occur in a single
  // sequence. The ordering of events within a step is strictly enforced.
  class Step {
   private:
    // A struct representing a pending transport Read.
    struct PendingRead {
      absl::AnyInvocable<void(absl::Status)> on_read;
      grpc_event_engine::experimental::SliceBuffer* buffer;
    };

   public:
    explicit Step(EventSequenceEndpoint* endpoint) : endpoint_(endpoint) {}
    Step(EventSequenceEndpoint* endpoint,
         std::deque<PendingRead>&& pending_reads)
        : endpoint_(endpoint),
          pending_reads_(std::forward<std::deque<PendingRead>>(pending_reads)) {
    }

    // Add a Read expectation to the front of the expectation queue. This will
    // be the next expectation to be enforced. Whenever the transport triggers a
    // Read that matches with this expectation, the slices will be used to
    // fulfill the read.
    void InsertReadAtHead(std::initializer_list<EventEngineSlice> slices) {
      GRPC_DCHECK(!done_);
      Expectation expectation(true, slices);
      LOG(INFO) << "[TransportTest] InsertReadAtHead: expectation: "
                << expectation;
      expectations_.push_front(std::move(expectation));
      Progress();
    }

    // Similar to InsertReadAtHead, but invokes the passed custom callback
    // before fulfilling the read.
    void InsertReadAtHead(
        absl::AnyInvocable<void(SliceBuffer&)> on_read_write_cb) {
      GRPC_DCHECK(!done_);
      Expectation expectation(true, absl::OkStatus(),
                              std::move(on_read_write_cb));
      LOG(INFO) << "[TransportTest] InsertReadAtHead: expectation: "
                << expectation;
      expectations_.push_front(std::move(expectation));
      Progress();
    }

    // Similar to InsertReadAtHead, but adds the expectation to the end of the
    // queue.
    void ThenPerformRead(std::initializer_list<EventEngineSlice> slices) {
      GRPC_DCHECK(!done_);
      Expectation expectation(true, slices);
      LOG(INFO) << "[TransportTest] ThenPerformRead: expectation: "
                << expectation;
      expectations_.push_back(std::move(expectation));
      Progress();
    }

    // Similar to ThenPerformRead, but invokes the passed custom callback
    // before fulfilling the read.
    void ThenPerformRead(
        absl::AnyInvocable<void(SliceBuffer&)> on_read_write_cb) {
      GRPC_DCHECK(!done_);
      Expectation expectation(true, absl::OkStatus(),
                              std::move(on_read_write_cb));
      LOG(INFO) << "[TransportTest] ThenPerformRead: expectation: "
                << expectation;
      expectations_.push_back(std::move(expectation));
      Progress();
    }

    // Adds an event to simulate a Read failure with the given non-OK `status`.
    void ThenFailRead(absl::Status status) {
      GRPC_DCHECK(!status.ok());
      GRPC_DCHECK(!done_);
      Expectation expectation(true, std::move(status), nullptr);
      LOG(INFO) << "[TransportTest] ThenFailRead: expectation: " << expectation;
      expectations_.push_back(std::move(expectation));
      Progress();
    }

    // Adds an event to expect a Write call from the transport. The written
    // slices are compared to the slices passed to this function.
    void ThenExpectWrite(std::initializer_list<EventEngineSlice> slices) {
      GRPC_DCHECK(!done_);
      Expectation expectation(false, slices);
      LOG(INFO) << "[TransportTest] ThenExpectWrite: expectation: "
                << expectation;
      expectations_.push_back(std::move(expectation));
    }

    // Similar to ThenExpectWrite, but invokes the passed custom callback
    // as a validator for the written slices.
    void ThenExpectWrite(
        absl::AnyInvocable<void(SliceBuffer&)> on_read_write_cb) {
      GRPC_DCHECK(!done_);
      Expectation expectation(false, absl::OkStatus(),
                              std::move(on_read_write_cb));
      LOG(INFO) << "[TransportTest] ThenExpectWrite: expectation: "
                << expectation;
      expectations_.push_back(std::move(expectation));
    }

    // Adds an event to simulate a Write failure with the given non-OK `status`.
    void ThenExpectWriteError(absl::Status status) {
      GRPC_DCHECK(!status.ok());
      GRPC_DCHECK(!done_);
      Expectation expectation(false, std::move(status), nullptr);
      LOG(INFO) << "[TransportTest] ThenExpectWriteError: expectation: "
                << expectation;
      expectations_.push_back(std::move(expectation));
    }

    // Blocks and processes events until `all` scripted Read and Write
    // expectations in this Step have been fulfilled. After invoking this
    // function, no more expectations can be added to this Step.
    void Wait() {
      GRPC_DCHECK(!done_);
      // Tick until all expectations are met
      while (!expectations_.empty()) {
        endpoint_->event_engine_->Tick();
      }
      done_ = true;
      LOG(INFO) << "[TransportTest] Step done";
    }

    bool AreExpectationsEmpty() const { return expectations_.empty(); }

    // Called by EventSequenceEndpoint when Transport calls Write. Uses the
    // validation logic in Expectation to validate the Write. This would fail in
    // the following cases:
    // 1. There are no expectations left in the queue.
    // 2. The next expectation is a Read.
    // 3. Validation logic in Expectation fails.
    // 4. The expectation simulates a Write error.
    // After successfully fulfilling a write expectation, Progress() is called
    // to fulfill any pending reads.
    bool OnWrite(
        absl::AnyInvocable<void(absl::Status)> on_writable,
        grpc_event_engine::experimental::SliceBuffer* buffer,
        grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs
            args) {
      LOG(INFO) << "[TransportTest] OnWrite: expectations_.size() = "
                << expectations_.size()
                << ", pending_reads_.size() = " << pending_reads_.size()
                << ", buffer->Length() = " << buffer->Length()
                << ", top expectation: "
                << (expectations_.empty()
                        ? std::string("none")
                        : absl::StrCat(expectations_.front()));
      if (expectations_.empty()) {
        ADD_FAILURE() << "Unexpected Write received of length: "
                      << buffer->Length();
        return false;
      }
      auto exp = std::move(expectations_.front());
      if (exp.is_read) {
        ADD_FAILURE() << "Expected Read but got Write of length: "
                      << buffer->Length();
        return false;
      }

      expectations_.pop_front();

      SliceBuffer tmp;
      // Convert EventEngine::SliceBuffer to core::SliceBuffer
      grpc_event_engine::experimental::SliceBuffer* ee_buffer = buffer;
      for (size_t i = 0; i < ee_buffer->c_slice_buffer()->count; ++i) {
        tmp.Append(
            Slice(grpc_slice_ref(ee_buffer->c_slice_buffer()->slices[i])));
      }
      ee_buffer->Clear();

      if (exp.on_read_write_cb != nullptr) {
        exp.on_read_write_cb(tmp);
      } else {
        GRPC_DCHECK(!exp.status.ok());
      }

      endpoint_->event_engine_->Run(
          [on_writable = std::move(on_writable),
           status = exp.status]() mutable { on_writable(std::move(status)); });

      Progress();
      return false;
    }

    // Called by EventSequenceEndpoint when Transport calls Read. These
    // transport Reads are added to a pending queue.
    bool OnRead(
        absl::AnyInvocable<void(absl::Status)> on_read,
        grpc_event_engine::experimental::SliceBuffer* buffer,
        grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs args) {
      pending_reads_.push_back(PendingRead{std::move(on_read), buffer});
      Progress();
      return false;
    }

    std::deque<PendingRead> TakePendingReads() {
      return std::move(pending_reads_);
    }

   private:
    // Progress any pending reads. This is needed as the promise based transport
    // can issue eager Reads. These reads will be stalled and added to the
    // pending_reads_ queue. The pending read will be fulfilled once there is a
    // Read expectation in the expectations_ queue. Additionally, if there is a
    // Read expectation and no pending transport Reads, the expectation will not
    // be fulfilled until the transport issues a Read.
    void Progress() {
      LOG(INFO) << "[TransportTest] Progress: expectations_.size() = "
                << expectations_.size()
                << ", pending_reads_.size() = " << pending_reads_.size()
                << ", top expectation: "
                << (expectations_.empty()
                        ? std::string("none")
                        : absl::StrCat(expectations_.front()));
      while (!expectations_.empty() && !pending_reads_.empty() &&
             expectations_.front().is_read) {
        auto exp = std::move(expectations_.front());
        expectations_.pop_front();
        auto read = std::move(pending_reads_.front());
        pending_reads_.pop_front();

        if (exp.status.ok()) {
          GRPC_DCHECK(exp.on_read_write_cb != nullptr);
          SliceBuffer tmp;
          exp.on_read_write_cb(tmp);
          for (size_t i = 0; i < tmp.Count(); ++i) {
            read.buffer->Append(EventEngineSlice(
                grpc_slice_ref(tmp.c_slice_buffer()->slices[i])));
          }
        }

        endpoint_->event_engine_->Run(
            [on_read = std::move(read.on_read), status = exp.status]() mutable {
              on_read(status);
            });
      }
    }

    struct Expectation {
      bool is_read;
      absl::Status status;
      absl::AnyInvocable<void(SliceBuffer&)> on_read_write_cb;

      Expectation(bool is_read, absl::Status status,
                  absl::AnyInvocable<void(SliceBuffer&)> on_read_write_cb)
          : is_read(is_read),
            status(std::move(status)),
            on_read_write_cb(std::move(on_read_write_cb)) {}

      Expectation(bool is_read, std::initializer_list<EventEngineSlice> slices)
          : is_read(is_read), status(absl::OkStatus()) {
        std::vector<EventEngineSlice> expected_slices;
        for (auto&& slice : slices) {
          expected_slices.emplace_back(slice.Copy());
        }
        if (is_read) {
          on_read_write_cb = [expected_slices = std::move(expected_slices)](
                                 SliceBuffer& buffer) {
            for (const auto& slice : expected_slices) {
              buffer.Append(Slice(grpc_slice_copy(slice.c_slice())));
            }
          };
        } else {
          on_read_write_cb = [expected_slices = std::move(expected_slices)](
                                 SliceBuffer& buffer) {
            SliceBuffer expected;
            for (const auto& slice : expected_slices) {
              expected.Append(Slice(grpc_slice_copy(slice.c_slice())));
            }
            EXPECT_EQ(buffer.JoinIntoString(), expected.JoinIntoString());
          };
        }
      }

      template <typename Sink>
      friend void AbslStringify(Sink& sink, const Expectation& expectation) {
        sink.Append(absl::StrCat(
            "Expectation{is_read: ", expectation.is_read,
            ", status: ", expectation.status, ", on_read_write_cb: ",
            expectation.on_read_write_cb
                ? TypeName<decltype(expectation.on_read_write_cb)>()
                : "nullptr"));
      }
    };

    EventSequenceEndpoint* endpoint_;
    std::deque<Expectation> expectations_;
    std::deque<PendingRead> pending_reads_;
    bool done_ = false;
  };

  static std::shared_ptr<EventSequenceEndpoint> Create(
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine) {
    auto endpoint = std::shared_ptr<EventSequenceEndpoint>(
        new EventSequenceEndpoint(std::move(event_engine)));
    endpoint->Init();
    return endpoint;
  }

  // EventEngineSequenceEndpoint is move constructible but not move assignable
  // or copyable.
  EventSequenceEndpoint(const EventSequenceEndpoint&) = delete;
  EventSequenceEndpoint& operator=(const EventSequenceEndpoint&) = delete;
  EventSequenceEndpoint& operator=(EventSequenceEndpoint&&) = delete;
  EventSequenceEndpoint(EventSequenceEndpoint&&) = default;

  PromiseEndpoint& promise_endpoint() { return promise_endpoint_; }

  // Returns a new `Step` context for scripting the next sequential batch
  // of transport operations. Must only fall exactly on Step boundaries.
  std::shared_ptr<Step> NewStep() {
    LOG(INFO) << "[TransportTest] NewStep";
    std::shared_ptr<Step> step;
    if (current_step_) {
      GRPC_DCHECK(current_step_->AreExpectationsEmpty());
      step = std::make_shared<Step>(this, current_step_->TakePendingReads());
    } else {
      step = std::make_shared<Step>(this);
    }
    current_step_ = step;
    GRPC_DCHECK(current_step_ != nullptr);
    return step;
  }

  bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_event_engine::experimental::SliceBuffer* buffer,
            grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs
                args) override {
    if (current_step_) {
      return current_step_->OnRead(std::move(on_read), buffer, args);
    }

    return false;
  }

  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             grpc_event_engine::experimental::SliceBuffer* buffer,
             grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs
                 args) override {
    if (current_step_) {
      return current_step_->OnWrite(std::move(on_writable), buffer,
                                    std::move(args));
    }

    return false;
  }

 private:
  explicit EventSequenceEndpoint(
      std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
          event_engine)
      : event_engine_(event_engine) {}

  // Initializes the internal PromiseEndpoint wrapping this mock instance.
  void Init() {
    promise_endpoint_ = PromiseEndpoint(
        std::make_unique<grpc_event_engine::experimental::MockEndpoint>(
            shared_from_this()),
        SliceBuffer());
  }

  const std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>
      event_engine_;
  PromiseEndpoint promise_endpoint_;
  std::shared_ptr<Step> current_step_;
};

}  // namespace testing
}  // namespace util
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_UTIL_TRANSPORT_TEST_H
