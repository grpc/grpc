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

#include "test/core/transport/chaotic_good/mock_promise_endpoint.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>

using EventEngineSlice = grpc_event_engine::experimental::Slice;
using grpc_event_engine::experimental::EventEngine;

using testing::WithArgs;

namespace grpc_core {
namespace chaotic_good {
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

}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core
