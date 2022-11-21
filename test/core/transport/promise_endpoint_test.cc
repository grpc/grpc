/*
 * Copyright 2022 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/core/lib/transport/promise_endpoint.h"

#include <memory>

#include <gtest/gtest.h>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"

#include <grpc/event_engine/event_engine.h>

namespace grpc {

namespace internal {

namespace testing {

class MockEndpoint
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  MockEndpoint() {
    ON_CALL(*this, GetPeerAddress)
        .WillByDefault(std::bind(&MockEndpoint::GetPeerAddressImpl, this));
    ON_CALL(*this, GetLocalAddress)
        .WillByDefault(std::bind(&MockEndpoint::GetLocalAddressImpl, this));
  }

  MOCK_METHOD(
      void, Read,
      (absl::AnyInvocable<void(absl::Status)> on_read,
       grpc_event_engine::experimental::SliceBuffer* buffer,
       const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
           args),
      (override));

  MOCK_METHOD(
      void, Write,
      (absl::AnyInvocable<void(absl::Status)> on_writable,
       grpc_event_engine::experimental::SliceBuffer* data,
       const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
           args),
      (override));

  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetPeerAddress, (), (const override));
  MOCK_METHOD(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&,
      GetLocalAddress, (), (const override));

  void set_peer_address(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          peer_address) {
    peer_address_ = peer_address;
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  peer_address() const {
    return peer_address_;
  }

  void set_local_address(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          local_address) {
    local_address_ = local_address;
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  local_address() const {
    return local_address_;
  }

 private:
  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address_;

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddressImpl() const {
    return peer_address();
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddressImpl() const {
    return local_address();
  }
};

class PromiseEndpointTest : public ::testing::Test {
 public:
  PromiseEndpointTest()
      : mock_endpoint_ptr_(new ::testing::NiceMock<MockEndpoint>()),
        mock_endpoint_(*mock_endpoint_ptr_),
        promise_endpoint_(
            std::unique_ptr<
                grpc_event_engine::experimental::EventEngine::Endpoint>(
                mock_endpoint_ptr_),
            grpc_core::SliceBuffer()) {}

 private:
  MockEndpoint* mock_endpoint_ptr_;

 protected:
  MockEndpoint& mock_endpoint_;
  grpc::internal::PromiseEndpoint promise_endpoint_;
};

TEST_F(PromiseEndpointTest, GetPeerAddress) {
  /// just some random bytes
  const char raw_peer_address[] = {0x55, 0x66, 0x01, 0x55, 0x66, 0x01};
  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address(
      reinterpret_cast<const sockaddr*>(raw_peer_address),
      sizeof(raw_peer_address));
  mock_endpoint_.set_peer_address(peer_address);

  EXPECT_CALL(mock_endpoint_, GetPeerAddress).Times(2);
  EXPECT_EQ(0, std::memcmp(promise_endpoint_.GetPeerAddress().address(),
                           peer_address.address(),
                           grpc_event_engine::experimental::EventEngine::
                               ResolvedAddress::MAX_SIZE_BYTES));
  EXPECT_EQ(peer_address.size(), promise_endpoint_.GetPeerAddress().size());
}

TEST_F(PromiseEndpointTest, GetLocalAddress) {
  /// just some random bytes
  const char raw_local_address[] = {0x52, 0x55, 0x66, 0x52, 0x55, 0x66};
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address(
      reinterpret_cast<const sockaddr*>(raw_local_address),
      sizeof(raw_local_address));
  mock_endpoint_.set_local_address(local_address);

  EXPECT_CALL(mock_endpoint_, GetLocalAddress).Times(2);
  EXPECT_EQ(0, std::memcmp(promise_endpoint_.GetLocalAddress().address(),
                           local_address.address(),
                           grpc_event_engine::experimental::EventEngine::
                               ResolvedAddress::MAX_SIZE_BYTES));
  EXPECT_EQ(local_address.size(), promise_endpoint_.GetLocalAddress().size());
}

}  // namespace testing

}  // namespace internal

}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
