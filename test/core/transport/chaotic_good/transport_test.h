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

#ifndef GRPC_TEST_CORE_TRANSPORT_CHAOTIC_GOOD_TRANSPORT_TEST_H
#define GRPC_TEST_CORE_TRANSPORT_CHAOTIC_GOOD_TRANSPORT_TEST_H

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "src/core/ext/transport/chaotic_good/frame.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"

namespace grpc_core {
namespace chaotic_good {
namespace testing {

class TransportTest : public ::testing::Test {
 protected:
  const std::shared_ptr<grpc_event_engine::experimental::FuzzingEventEngine>&
  event_engine() {
    return event_engine_;
  }

  MemoryAllocator* memory_allocator() { return &allocator_; }

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
  MemoryAllocator allocator_ = MakeResourceQuota("test-quota")
                                   ->memory_quota()
                                   ->CreateMemoryAllocator("test-allocator");
};

grpc_event_engine::experimental::Slice SerializedFrameHeader(
    FrameType type, uint8_t flags, uint32_t stream_id, uint32_t header_length,
    uint32_t message_length, uint32_t message_padding, uint32_t trailer_length);

grpc_event_engine::experimental::Slice Zeros(uint32_t length);

}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_CHAOTIC_GOOD_TRANSPORT_TEST_H
