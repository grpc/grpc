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

#include "src/core/lib/transport/promise_endpoint.h"

// IWYU pragma: no_include <sys/socket.h>

#include <cstring>
#include <memory>
#include <string>
#include <tuple>

#include "absl/functional/any_invocable.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/port.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::MockFunction;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::StrictMock;
using testing::WithArg;
using testing::WithArgs;

namespace grpc_core {
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

class MockActivity : public Activity, public Wakeable {
 public:
  MOCK_METHOD(void, WakeupRequested, ());

  void ForceImmediateRepoll(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Orphan() override {}
  Waker MakeOwningWaker() override { return Waker(this, 0); }
  Waker MakeNonOwningWaker() override { return Waker(this, 0); }
  void Wakeup(WakeupMask /*mask*/) override { WakeupRequested(); }
  void WakeupAsync(WakeupMask /*mask*/) override { WakeupRequested(); }
  void Drop(WakeupMask /*mask*/) override {}
  std::string DebugTag() const override { return "MockActivity"; }
  std::string ActivityDebugTag(WakeupMask /*mask*/) const override {
    return DebugTag();
  }

  void Activate() {
    if (scoped_activity_ == nullptr) {
      scoped_activity_ = std::make_unique<ScopedActivity>(this);
    }
  }

  void Deactivate() { scoped_activity_.reset(); }

 private:
  std::unique_ptr<ScopedActivity> scoped_activity_;
};

class PromiseEndpointTest : public ::testing::Test {
 public:
  PromiseEndpointTest()
      : mock_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        mock_endpoint_(*mock_endpoint_ptr_),
        promise_endpoint_(
            std::unique_ptr<
                grpc_event_engine::experimental::EventEngine::Endpoint>(
                mock_endpoint_ptr_),
            SliceBuffer()) {}

 private:
  MockEndpoint* mock_endpoint_ptr_;

 protected:
  MockEndpoint& mock_endpoint_;
  PromiseEndpoint promise_endpoint_;

  const absl::Status kDummyErrorStatus =
      absl::ErrnoToStatus(5566, "just an error");
  static constexpr size_t kDummyRequestSize = 5566u;
};

TEST_F(PromiseEndpointTest, OneReadSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  auto promise = promise_endpoint_.Read(kBuffer.size());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadFailed) {
  MockActivity activity;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [this](absl::AnyInvocable<void(absl::Status)> read_callback) {
            // Mock EventEngine enpoint read fails.
            read_callback(this->kDummyErrorStatus);
            return false;
          }));
  auto promise = promise_endpoint_.Read(kDummyRequestSize);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadsSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  Sequence s;
  EXPECT_CALL(mock_endpoint_, Read)
      .InSequence(s)
      .WillOnce(WithArg<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer.substr(0, 4)));
            buffer->Append(std::move(slice));
            return true;
          }));
  EXPECT_CALL(mock_endpoint_, Read)
      .InSequence(s)
      .WillOnce(WithArg<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer.substr(4)));
            buffer->Append(std::move(slice));
            return true;
          }));
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer.substr(4));
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingReadSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  absl::AnyInvocable<void(absl::Status)> read_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0, 1>(
          [&read_callback, &kBuffer](
              absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_event_engine::experimental::SliceBuffer* buffer) {
            read_callback = std::move(on_read);
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            // Return false to mock EventEngine read not finish..
            return false;
          }));
  auto promise = promise_endpoint_.Read(kBuffer.size());
  EXPECT_TRUE(promise().pending());
  // Mock EventEngine read succeeds, and promise resolves.
  read_callback(absl::OkStatus());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingReadFailed) {
  MockActivity activity;
  absl::AnyInvocable<void(absl::Status)> read_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [&read_callback](absl::AnyInvocable<void(absl::Status)> on_read) {
            read_callback = std::move(on_read);
            // Return false to mock EventEngine read not finish.
            return false;
          }));
  auto promise = promise_endpoint_.Read(kDummyRequestSize);
  EXPECT_TRUE(promise().pending());
  // Mock EventEngine read fails, and promise returns error.
  read_callback(kDummyErrorStatus);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  auto promise = promise_endpoint_.ReadSlice(kBuffer.size());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->as_string_view(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceFailed) {
  MockActivity activity;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [this](absl::AnyInvocable<void(absl::Status)> read_callback) {
            // Mock EventEngine enpoint read fails.
            read_callback(this->kDummyErrorStatus);
            return false;
          }));
  auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadSlicesSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  Sequence s;
  EXPECT_CALL(mock_endpoint_, Read)
      .InSequence(s)
      .WillOnce(WithArg<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer.substr(0, 4)));
            buffer->Append(std::move(slice));
            return true;
          }));
  EXPECT_CALL(mock_endpoint_, Read)
      .InSequence(s)
      .WillOnce(WithArg<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer.substr(4)));
            buffer->Append(std::move(slice));
            return true;
          }));
  {
    auto promise = promise_endpoint_.ReadSlice(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer.substr(4));
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingReadSliceSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  absl::AnyInvocable<void(absl::Status)> read_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0, 1>(
          [&read_callback, &kBuffer](
              absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_event_engine::experimental::SliceBuffer* buffer) {
            read_callback = std::move(on_read);
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            // Return false to mock EventEngine read not finish..
            return false;
          }));
  auto promise = promise_endpoint_.ReadSlice(kBuffer.size());
  EXPECT_TRUE(promise().pending());
  // Mock EventEngine read succeeds, and promise resolves.
  read_callback(absl::OkStatus());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->as_string_view(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingReadSliceFailed) {
  MockActivity activity;
  absl::AnyInvocable<void(absl::Status)> read_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [&read_callback](absl::AnyInvocable<void(absl::Status)> on_read) {
            read_callback = std::move(on_read);
            // Return false to mock EventEngine read not finish.
            return false;
          }));
  auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
  EXPECT_TRUE(promise().pending());
  // Mock EventEngine read fails, and promise returns error.
  read_callback(kDummyErrorStatus);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadByteSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01};
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  auto promise = promise_endpoint_.ReadByte();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(*poll.value(), kBuffer[0]);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadByteFailed) {
  MockActivity activity;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [this](absl::AnyInvocable<void(absl::Status)> read_callback) {
            // Mock EventEngine enpoint read fails.
            read_callback(this->kDummyErrorStatus);
            return false;
          }));
  auto promise = promise_endpoint_.ReadByte();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadBytesSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArg<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    auto promise = promise_endpoint_.ReadByte();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(*poll.value(), kBuffer[i]);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingReadByteSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01};
  absl::AnyInvocable<void(absl::Status)> read_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0, 1>(
          [&read_callback, &kBuffer](
              absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_event_engine::experimental::SliceBuffer* buffer) {
            read_callback = std::move(on_read);
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            // Return false to mock EventEngine read not finish..
            return false;
          }));
  auto promise = promise_endpoint_.ReadByte();
  ASSERT_TRUE(promise().pending());
  // Mock EventEngine read succeeds, and promise resolves.
  read_callback(absl::OkStatus());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(*poll.value(), kBuffer[0]);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePengingReadByteFailed) {
  MockActivity activity;
  absl::AnyInvocable<void(absl::Status)> read_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [&read_callback](absl::AnyInvocable<void(absl::Status)> on_read) {
            read_callback = std::move(on_read);
            // Return false to mock EventEngine read not finish.
            return false;
          }));
  auto promise = promise_endpoint_.ReadByte();
  ASSERT_TRUE(promise().pending());
  // Mock EventEngine read fails, and promise returns error.
  read_callback(kDummyErrorStatus);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneWriteSuccessful) {
  MockActivity activity;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Write).WillOnce(Return(true));
  auto promise = promise_endpoint_.Write(SliceBuffer());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(absl::OkStatus(), poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneWriteFailed) {
  MockActivity activity;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Write)
      .WillOnce(
          WithArgs<0>([this](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(this->kDummyErrorStatus);
            return false;
          }));
  auto promise = promise_endpoint_.Write(SliceBuffer());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(kDummyErrorStatus, poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingWriteSuccessful) {
  MockActivity activity;
  absl::AnyInvocable<void(absl::Status)> write_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Write)
      .WillOnce(WithArgs<0, 1>(
          [&write_callback](
              absl::AnyInvocable<void(absl::Status)> on_write,
              grpc_event_engine::experimental::SliceBuffer* buffer) {
            write_callback = std::move(on_write);
            // Schedule mock_endpoint to write buffer.
            buffer->Append(grpc_event_engine::experimental::Slice());
            // Return false to mock EventEngine write pending..
            return false;
          }));
  auto promise = promise_endpoint_.Write(SliceBuffer());
  EXPECT_TRUE(promise().pending());
  // Mock EventEngine write succeeds, and promise resolves.
  write_callback(absl::OkStatus());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(absl::OkStatus(), poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OnePendingWriteFailed) {
  MockActivity activity;
  absl::AnyInvocable<void(absl::Status)> write_callback;
  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Write)
      .WillOnce(WithArg<0>(
          [&write_callback](absl::AnyInvocable<void(absl::Status)> on_write) {
            write_callback = std::move(on_write);
            // Return false to mock EventEngine write pending..
            return false;
          }));
  auto promise = promise_endpoint_.Write(SliceBuffer());
  EXPECT_TRUE(promise().pending());
  write_callback(kDummyErrorStatus);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(kDummyErrorStatus, poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, GetPeerAddress) {
  const char raw_test_address[] = {0x55, 0x66, 0x01, 0x55, 0x66, 0x01};
  grpc_event_engine::experimental::EventEngine::ResolvedAddress test_address(
      reinterpret_cast<const sockaddr*>(raw_test_address),
      sizeof(raw_test_address));
  EXPECT_CALL(mock_endpoint_, GetPeerAddress).WillOnce(ReturnRef(test_address));
  auto peer_address = promise_endpoint_.GetPeerAddress();
  EXPECT_EQ(0, std::memcmp(test_address.address(), test_address.address(),
                           test_address.size()));
  EXPECT_EQ(test_address.size(), peer_address.size());
}

TEST_F(PromiseEndpointTest, GetLocalAddress) {
  const char raw_test_address[] = {0x52, 0x55, 0x66, 0x52, 0x55, 0x66};
  grpc_event_engine::experimental::EventEngine::ResolvedAddress test_address(
      reinterpret_cast<const sockaddr*>(raw_test_address),
      sizeof(raw_test_address));
  EXPECT_CALL(mock_endpoint_, GetLocalAddress)
      .WillOnce(ReturnRef(test_address));
  auto local_address = promise_endpoint_.GetLocalAddress();
  EXPECT_EQ(0, std::memcmp(test_address.address(), local_address.address(),
                           test_address.size()));
  EXPECT_EQ(test_address.size(), local_address.size());
}

class MultiplePromiseEndpointTest : public ::testing::Test {
 public:
  MultiplePromiseEndpointTest()
      : first_mock_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        second_mock_endpoint_ptr_(new StrictMock<MockEndpoint>()),
        first_mock_endpoint_(*first_mock_endpoint_ptr_),
        second_mock_endpoint_(*second_mock_endpoint_ptr_),
        first_promise_endpoint_(
            std::unique_ptr<
                grpc_event_engine::experimental::EventEngine::Endpoint>(
                first_mock_endpoint_ptr_),
            SliceBuffer()),
        second_promise_endpoint_(
            std::unique_ptr<
                grpc_event_engine::experimental::EventEngine::Endpoint>(
                second_mock_endpoint_ptr_),
            SliceBuffer()) {}

 private:
  MockEndpoint* first_mock_endpoint_ptr_;
  MockEndpoint* second_mock_endpoint_ptr_;

 protected:
  MockEndpoint& first_mock_endpoint_;
  MockEndpoint& second_mock_endpoint_;
  PromiseEndpoint first_promise_endpoint_;
  PromiseEndpoint second_promise_endpoint_;

  const absl::Status kDummyErrorStatus =
      absl::ErrnoToStatus(5566, "just an error");
  static constexpr size_t kDummyRequestSize = 5566u;
};

TEST_F(MultiplePromiseEndpointTest, JoinReadsSuccessful) {
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  EXPECT_CALL(first_mock_endpoint_, Read)
      .WillOnce(WithArgs<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  EXPECT_CALL(second_mock_endpoint_, Read)
      .WillOnce(WithArgs<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto activity = MakeActivity(
      [this, &kBuffer] {
        return Seq(Join(this->first_promise_endpoint_.Read(kBuffer.size()),
                        this->second_promise_endpoint_.Read(kBuffer.size())),
                   [](std::tuple<absl::StatusOr<SliceBuffer>,
                                 absl::StatusOr<SliceBuffer>>
                          ret) {
                     // Both reads finish with `absl::OkStatus`.
                     EXPECT_TRUE(std::get<0>(ret).ok());
                     EXPECT_TRUE(std::get<1>(ret).ok());
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST_F(MultiplePromiseEndpointTest, JoinOneReadSuccessfulOneReadFailed) {
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  EXPECT_CALL(first_mock_endpoint_, Read)
      .WillOnce(WithArgs<1>(
          [&kBuffer](grpc_event_engine::experimental::SliceBuffer* buffer) {
            // Schedule mock_endpoint to read buffer.
            grpc_event_engine::experimental::Slice slice(
                grpc_slice_from_cpp_string(kBuffer));
            buffer->Append(std::move(slice));
            return true;
          }));
  EXPECT_CALL(second_mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [this](absl::AnyInvocable<void(absl::Status)> read_callback) {
            // Mock EventEngine enpoint read fails.
            read_callback(this->kDummyErrorStatus);
            return false;
          }));
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(kDummyErrorStatus));
  auto activity = MakeActivity(
      [this, &kBuffer] {
        return Seq(
            Join(this->first_promise_endpoint_.Read(kBuffer.size()),
                 this->second_promise_endpoint_.Read(this->kDummyRequestSize)),
            [this](std::tuple<absl::StatusOr<SliceBuffer>,
                              absl::StatusOr<SliceBuffer>>
                       ret) {
              // One read finishes with `absl::OkStatus` and the other read
              // fails.
              EXPECT_TRUE(std::get<0>(ret).ok());
              EXPECT_FALSE(std::get<1>(ret).ok());
              EXPECT_EQ(std::get<1>(ret).status(), this->kDummyErrorStatus);
              return this->kDummyErrorStatus;
            });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST_F(MultiplePromiseEndpointTest, JoinReadsFailed) {
  EXPECT_CALL(first_mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [this](absl::AnyInvocable<void(absl::Status)> read_callback) {
            // Mock EventEngine enpoint read fails.
            read_callback(this->kDummyErrorStatus);
            return false;
          }));
  EXPECT_CALL(second_mock_endpoint_, Read)
      .WillOnce(WithArgs<0>(
          [this](absl::AnyInvocable<void(absl::Status)> read_callback) {
            // Mock EventEngine enpoint read fails.
            read_callback(this->kDummyErrorStatus);
            return false;
          }));
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(kDummyErrorStatus));
  auto activity = MakeActivity(
      [this] {
        return Seq(
            Join(this->first_promise_endpoint_.Read(this->kDummyRequestSize),
                 this->second_promise_endpoint_.Read(this->kDummyRequestSize)),
            [this](std::tuple<absl::StatusOr<SliceBuffer>,
                              absl::StatusOr<SliceBuffer>>
                       ret) {
              // Both reads finish with errors.
              EXPECT_FALSE(std::get<0>(ret).ok());
              EXPECT_FALSE(std::get<1>(ret).ok());
              EXPECT_EQ(std::get<0>(ret).status(), this->kDummyErrorStatus);
              EXPECT_EQ(std::get<1>(ret).status(), this->kDummyErrorStatus);
              return this->kDummyErrorStatus;
            });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST_F(MultiplePromiseEndpointTest, JoinWritesSuccessful) {
  EXPECT_CALL(first_mock_endpoint_, Write).WillOnce(Return(true));
  EXPECT_CALL(second_mock_endpoint_, Write).WillOnce(Return(true));
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(absl::OkStatus()));
  auto activity = MakeActivity(
      [this] {
        return Seq(Join(this->first_promise_endpoint_.Write(SliceBuffer()),
                        this->second_promise_endpoint_.Write(SliceBuffer())),
                   [](std::tuple<absl::Status, absl::Status> ret) {
                     // Both writes finish with `absl::OkStatus`.
                     EXPECT_TRUE(std::get<0>(ret).ok());
                     EXPECT_TRUE(std::get<1>(ret).ok());
                     return absl::OkStatus();
                   });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST_F(MultiplePromiseEndpointTest, JoinOneWriteSuccessfulOneWriteFailed) {
  EXPECT_CALL(first_mock_endpoint_, Write).WillOnce(Return(true));
  EXPECT_CALL(second_mock_endpoint_, Write)
      .WillOnce(
          WithArgs<0>([this](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(this->kDummyErrorStatus);
            return false;
          }));
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(kDummyErrorStatus));
  auto activity = MakeActivity(
      [this] {
        return Seq(Join(this->first_promise_endpoint_.Write(SliceBuffer()),
                        this->second_promise_endpoint_.Write(SliceBuffer())),
                   [this](std::tuple<absl::Status, absl::Status> ret) {
                     // One write finish with `absl::OkStatus` and the other
                     // write fails.
                     EXPECT_TRUE(std::get<0>(ret).ok());
                     EXPECT_FALSE(std::get<1>(ret).ok());
                     EXPECT_EQ(std::get<1>(ret), this->kDummyErrorStatus);
                     return this->kDummyErrorStatus;
                   });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

TEST_F(MultiplePromiseEndpointTest, JoinWritesFailed) {
  EXPECT_CALL(first_mock_endpoint_, Write)
      .WillOnce(
          WithArgs<0>([this](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(this->kDummyErrorStatus);
            return false;
          }));
  EXPECT_CALL(second_mock_endpoint_, Write)
      .WillOnce(
          WithArgs<0>([this](absl::AnyInvocable<void(absl::Status)> on_write) {
            on_write(this->kDummyErrorStatus);
            return false;
          }));
  StrictMock<MockFunction<void(absl::Status)>> on_done;
  EXPECT_CALL(on_done, Call(kDummyErrorStatus));
  auto activity = MakeActivity(
      [this] {
        return Seq(Join(this->first_promise_endpoint_.Write(SliceBuffer()),
                        this->second_promise_endpoint_.Write(SliceBuffer())),
                   [this](std::tuple<absl::Status, absl::Status> ret) {
                     // Both writes fail with errors.
                     EXPECT_FALSE(std::get<0>(ret).ok());
                     EXPECT_FALSE(std::get<1>(ret).ok());
                     EXPECT_EQ(std::get<0>(ret), this->kDummyErrorStatus);
                     EXPECT_EQ(std::get<1>(ret), this->kDummyErrorStatus);
                     return this->kDummyErrorStatus;
                   });
      },
      InlineWakeupScheduler(),
      [&on_done](absl::Status status) { on_done.Call(std::move(status)); });
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
