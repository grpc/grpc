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

#ifndef GRPC_TEST_CORE_TRANSPORT_CHAOTIC_GOOD_MOCK_PROMISE_ENDPOINT_H
#define GRPC_TEST_CORE_TRANSPORT_CHAOTIC_GOOD_MOCK_PROMISE_ENDPOINT_H

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/transport/promise_endpoint.h"

namespace grpc_core {
namespace chaotic_good {
namespace testing {

class MockEndpoint
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  MOCK_METHOD(
      bool, Read,
      (absl::AnyInvocable<void(absl::Status)> on_read,
       grpc_event_engine::experimental::SliceBuffer* buffer,
       const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
           args),
      (override));

  MOCK_METHOD(
      bool, Write,
      (absl::AnyInvocable<void(absl::Status)> on_writable,
       grpc_event_engine::experimental::SliceBuffer* data,
       const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
           args),
      (override));

  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetPeerAddress, (), (const, override));
  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetLocalAddress, (), (const, override));
};

struct MockPromiseEndpoint {
  ::testing::StrictMock<MockEndpoint>* endpoint =
      new ::testing::StrictMock<MockEndpoint>();
  PromiseEndpoint promise_endpoint = PromiseEndpoint(
      std::unique_ptr<::testing::StrictMock<MockEndpoint>>(endpoint),
      SliceBuffer());
  ::testing::Sequence read_sequence;
  ::testing::Sequence write_sequence;
  void ExpectRead(
      std::initializer_list<grpc_event_engine::experimental::Slice> slices_init,
      grpc_event_engine::experimental::EventEngine* schedule_on_event_engine);
  void ExpectWrite(
      std::initializer_list<grpc_event_engine::experimental::Slice> slices,
      grpc_event_engine::experimental::EventEngine* schedule_on_event_engine);
};

}  // namespace testing
}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TRANSPORT_CHAOTIC_GOOD_MOCK_PROMISE_ENDPOINT_H
