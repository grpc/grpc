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

#include "test/core/transport/util/mock_promise_endpoint.h"

#include <grpc/event_engine/event_engine.h>

#include <cstddef>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/notification.h"

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using grpc_event_engine::experimental::EventEngine;

using testing::WithArgs;

namespace grpc_core {
namespace util {
namespace testing {

void MockPromiseEndpoint::ExpectRead(
    std::initializer_list<EventEngineSlice> slices_init,
    EventEngine* schedule_on_event_engine) {
  std::vector<EventEngineSlice> slices;
  for (auto&& slice : slices_init) slices.emplace_back(slice.Copy());
  EXPECT_CALL(*endpoint, Read)
      .InSequence(read_sequence)
      .WillOnce(WithArgs<0, 1>(
          [slices = std::move(slices), schedule_on_event_engine](
              absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_event_engine::experimental::SliceBuffer* buffer) mutable {
            for (auto& slice : slices) {
              buffer->Append(std::move(slice));
            }
            if (schedule_on_event_engine != nullptr) {
              schedule_on_event_engine->Run(
                  [on_read = std::move(on_read)]() mutable {
                    on_read(absl::OkStatus());
                  });
              return false;
            } else {
              return true;
            }
          }));
}

namespace {

class DelayedRead {
 public:
  explicit DelayedRead(EventEngine* event_engine, absl::Status status)
      : status_(status), event_engine_(event_engine->shared_from_this()) {}

  void GotOnRead(absl::AnyInvocable<void(absl::Status)> on_read) {
    on_read_ = std::move(on_read);
    if (state_.fetch_add(1) == 1) {
      Done();
    }
  }

  void AllowOnRead() {
    if (state_.fetch_add(1) == 1) {
      Done();
    }
  }

 private:
  void Done() {
    event_engine_->Run([event_engine = event_engine_,
                        on_read = std::move(on_read_),
                        status = status_]() mutable { on_read(status); });
    event_engine_.reset();
  }

  absl::AnyInvocable<void(absl::Status)> on_read_;
  absl::Status status_;
  std::shared_ptr<EventEngine> event_engine_;
  std::atomic<int> state_{0};
};

}  // namespace

absl::AnyInvocable<void()> MockPromiseEndpoint::ExpectDelayedRead(
    std::initializer_list<EventEngineSlice> slices_init,
    EventEngine* schedule_on_event_engine) {
  std::shared_ptr<DelayedRead> delayed_read =
      std::make_shared<DelayedRead>(schedule_on_event_engine, absl::OkStatus());
  std::vector<EventEngineSlice> slices;
  for (auto&& slice : slices_init) slices.emplace_back(slice.Copy());
  EXPECT_CALL(*endpoint, Read)
      .InSequence(read_sequence)
      .WillOnce(WithArgs<0, 1>(
          [slices = std::move(slices), delayed_read](
              absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_event_engine::experimental::SliceBuffer* buffer) mutable {
            for (auto& slice : slices) {
              buffer->Append(std::move(slice));
            }
            delayed_read->GotOnRead(std::move(on_read));
            return false;
          }));
  return [delayed_read]() { delayed_read->AllowOnRead(); };
}

void MockPromiseEndpoint::ExpectReadClose(
    absl::Status status,
    grpc_event_engine::experimental::EventEngine* schedule_on_event_engine) {
  DCHECK_NE(status, absl::OkStatus());
  DCHECK_NE(schedule_on_event_engine, nullptr);
  EXPECT_CALL(*endpoint, Read)
      .InSequence(read_sequence)
      .WillOnce(WithArgs<0, 1>(
          [status = std::move(status), schedule_on_event_engine](
              absl::AnyInvocable<void(absl::Status)> on_read,
              GRPC_UNUSED grpc_event_engine::experimental::SliceBuffer*
                  buffer) {
            schedule_on_event_engine->Run(
                [on_read = std::move(on_read), status]() mutable {
                  on_read(status);
                });
            return false;
          }));
}

absl::AnyInvocable<void()> MockPromiseEndpoint::ExpectDelayedReadClose(
    absl::Status status,
    grpc_event_engine::experimental::EventEngine* schedule_on_event_engine) {
  std::shared_ptr<DelayedRead> delayed_read_close =
      std::make_shared<DelayedRead>(schedule_on_event_engine, status);
  DCHECK_NE(schedule_on_event_engine, nullptr);
  EXPECT_CALL(*endpoint, Read)
      .InSequence(read_sequence)
      .WillOnce(WithArgs<0, 1>(
          [delayed_read_close](
              absl::AnyInvocable<void(absl::Status)> on_read,
              GRPC_UNUSED grpc_event_engine::experimental::SliceBuffer*
                  buffer) {
            delayed_read_close->GotOnRead(std::move(on_read));
            return false;
          }));
  return [delayed_read_close, status, schedule_on_event_engine]() {
    schedule_on_event_engine->Run([delayed_read_close, status]() mutable {
      delayed_read_close->AllowOnRead();
    });
  };
}

void MockPromiseEndpoint::ExpectWrite(
    std::initializer_list<EventEngineSlice> slices,
    EventEngine* schedule_on_event_engine) {
  SliceBuffer expect;
  for (auto&& slice : slices) {
    expect.Append(grpc_event_engine::experimental::internal::SliceCast<Slice>(
        slice.Copy()));
  }
  EXPECT_CALL(*endpoint, Write)
      .InSequence(write_sequence)
      .WillOnce(WithArgs<0, 1>(
          [expect = expect.JoinIntoString(), schedule_on_event_engine](
              absl::AnyInvocable<void(absl::Status)> on_writable,
              grpc_event_engine::experimental::SliceBuffer* buffer) mutable {
            SliceBuffer tmp;
            grpc_slice_buffer_swap(buffer->c_slice_buffer(),
                                   tmp.c_slice_buffer());
            EXPECT_EQ(tmp.JoinIntoString(), expect);
            if (schedule_on_event_engine != nullptr) {
              schedule_on_event_engine->Run(
                  [on_writable = std::move(on_writable)]() mutable {
                    on_writable(absl::OkStatus());
                  });
              return false;
            } else {
              return true;
            }
          }));
}

void MockPromiseEndpoint::ExpectWriteWithCallback(
    std::initializer_list<EventEngineSlice> slices,
    EventEngine* schedule_on_event_engine,
    absl::AnyInvocable<void(SliceBuffer&, SliceBuffer&)> callback) {
  SliceBuffer expect;
  for (auto&& slice : slices) {
    expect.Append(grpc_event_engine::experimental::internal::SliceCast<Slice>(
        slice.Copy()));
  }
  EXPECT_CALL(*endpoint, Write)
      .InSequence(write_sequence)
      .WillOnce(WithArgs<0, 1>(
          [expect = std::move(expect), schedule_on_event_engine,
           callback = std::move(callback)](
              absl::AnyInvocable<void(absl::Status)> on_writable,
              grpc_event_engine::experimental::SliceBuffer* buffer) mutable {
            SliceBuffer tmp;
            grpc_slice_buffer_swap(buffer->c_slice_buffer(),
                                   tmp.c_slice_buffer());
            callback(tmp, expect);

            if (schedule_on_event_engine != nullptr) {
              schedule_on_event_engine->Run(
                  [on_writable = std::move(on_writable)]() mutable {
                    on_writable(absl::OkStatus());
                  });
              return false;
            } else {
              return true;
            }
          }));
}

void MockPromiseEndpoint::CaptureWrites(SliceBuffer& writes,
                                        EventEngine* schedule_on_event_engine) {
  EXPECT_CALL(*endpoint, Write)
      .Times(::testing::AtLeast(0))
      .WillRepeatedly(WithArgs<0, 1>(
          [writes = &writes, schedule_on_event_engine](
              absl::AnyInvocable<void(absl::Status)> on_writable,
              grpc_event_engine::experimental::SliceBuffer* buffer) {
            SliceBuffer temp;
            grpc_slice_buffer_swap(temp.c_slice_buffer(),
                                   buffer->c_slice_buffer());
            writes->Append(temp);
            if (schedule_on_event_engine != nullptr) {
              schedule_on_event_engine->Run(
                  [on_writable = std::move(on_writable)]() mutable {
                    on_writable(absl::OkStatus());
                  });
              return false;
            } else {
              return true;
            }
          }));
}

}  // namespace testing
}  // namespace util
}  // namespace grpc_core
