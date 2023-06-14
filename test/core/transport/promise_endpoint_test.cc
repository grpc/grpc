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

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <tuple>

#include "absl/functional/any_invocable.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/port.h>  // IWYU pragma: keep
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/detail/basic_join.h"
#include "src/core/lib/promise/join.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/promise/test_wakeup_schedulers.h"

using testing::MockFunction;
using testing::StrictMock;

namespace grpc_core {
namespace testing {

class MockEndpoint
    : public grpc_event_engine::experimental::EventEngine::Endpoint {
 public:
  MockEndpoint() {
    ON_CALL(*this, Read)
        .WillByDefault(std::bind(&MockEndpoint::ReadImpl, this,
                                 std::placeholders::_1, std::placeholders::_2,
                                 std::placeholders::_3));
    ON_CALL(*this, Write)
        .WillByDefault(std::bind(&MockEndpoint::WriteImpl, this,
                                 std::placeholders::_1, std::placeholders::_2,
                                 std::placeholders::_3));
    ON_CALL(*this, GetPeerAddress)
        .WillByDefault(std::bind(&MockEndpoint::GetPeerAddressImpl, this));
    ON_CALL(*this, GetLocalAddress)
        .WillByDefault(std::bind(&MockEndpoint::GetLocalAddressImpl, this));
  }

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

  void set_peer_address(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          peer_address) {
    peer_address_ = peer_address;
  }

  void set_local_address(
      const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
          local_address) {
    local_address_ = local_address;
  }

  // Only for an error read task.
  void ScheduleReadTask(const absl::Status& status, bool ready = false) {
    GPR_ASSERT(!status.ok());
    read_task_queue_.push({ready, status, {}, {}, {}});
  }

  // Only for a successful read task.
  void ScheduleReadTask(const std::string& buffer, bool ready = false) {
    read_task_queue_.push({ready, absl::OkStatus(), buffer, {}, {}});
  }

  void MarkNextReadReady() {
    GPR_ASSERT(!read_task_queue_.empty());
    GPR_ASSERT(!read_task_queue_.front().ready);

    read_task_queue_.front().ready = true;
    if (read_task_queue_.front().callback.has_value()) {
      if (read_task_queue_.front().status.ok()) {
        GPR_ASSERT(read_task_queue_.front().source_buffer.has_value());
        grpc_event_engine::experimental::Slice slice(grpc_slice_from_cpp_string(
            *(read_task_queue_.front().source_buffer)));
        GPR_ASSERT(read_task_queue_.front().destination_buffer.has_value());
        (*read_task_queue_.front().destination_buffer)
            ->Append(std::move(slice));
      }
      auto callback = std::move(*read_task_queue_.front().callback);
      auto status = read_task_queue_.front().status;
      read_task_queue_.pop();
      callback(status);
    }
  }

  void ScheduleWriteTask(const absl::Status& status, bool ready = false) {
    write_task_queue_.push({ready, status, {}});
  }

  void MarkNextWriteReady() {
    GPR_ASSERT(!write_task_queue_.empty());
    GPR_ASSERT(!write_task_queue_.front().ready);

    write_task_queue_.front().ready = true;
    if (write_task_queue_.front().callback.has_value()) {
      (*write_task_queue_.front().callback)(write_task_queue_.front().status);
      write_task_queue_.pop();
    }
  }

 private:
  struct ReadTask {
    bool ready;
    const absl::Status status;
    absl::optional<std::string> source_buffer;
    absl::optional<grpc_event_engine::experimental::SliceBuffer*>
        destination_buffer;
    absl::optional<absl::AnyInvocable<void(absl::Status)>> callback;
  };
  std::queue<ReadTask> read_task_queue_;

  bool ReadImpl(
      absl::AnyInvocable<void(absl::Status)> on_read,
      grpc_event_engine::experimental::SliceBuffer* buffer,
      const grpc_event_engine::experimental::EventEngine::Endpoint::ReadArgs*
      /* args */) {
    // Should always schedule a read first.
    GPR_ASSERT(!read_task_queue_.empty());
    GPR_ASSERT(buffer != nullptr);

    if (read_task_queue_.front().ready) {
      if (read_task_queue_.front().status.ok()) {
        GPR_ASSERT(read_task_queue_.front().source_buffer.has_value());
        grpc_event_engine::experimental::Slice slice(grpc_slice_from_cpp_string(
            *(read_task_queue_.front().source_buffer)));
        buffer->Append(std::move(slice));
        read_task_queue_.pop();
        return true;  // Returns true since the data is already available.
      } else {
        auto status = read_task_queue_.front().status;
        read_task_queue_.pop();
        on_read(status);
      }
    } else {
      read_task_queue_.front().destination_buffer = buffer;
      read_task_queue_.front().callback = std::move(on_read);
    }

    return false;
  }

  struct WriteTask {
    bool ready;
    const absl::Status status;
    absl::optional<absl::AnyInvocable<void(absl::Status)>> callback;
  };
  std::queue<WriteTask> write_task_queue_;

  bool WriteImpl(
      absl::AnyInvocable<void(absl::Status)> on_writable,
      grpc_event_engine::experimental::SliceBuffer* /* data */,
      const grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs*
      /* args */) {
    // Should always schedule a write first.
    GPR_ASSERT(!write_task_queue_.empty());

    if (write_task_queue_.front().ready) {
      if (write_task_queue_.front().status.ok()) {
        write_task_queue_.pop();
        return true;  // Returns true since the data is already available.
      } else {
        auto status = write_task_queue_.front().status;
        write_task_queue_.pop();
        on_writable(status);
      }
    } else {
      write_task_queue_.front().callback = std::move(on_writable);
    }

    return false;
  }

  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address_;
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address_;

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddressImpl() const {
    return peer_address_;
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddressImpl() const {
    return local_address_;
  }
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
      : mock_endpoint_ptr_(new ::testing::NiceMock<MockEndpoint>()),
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
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.Read(kBuffer.size());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.Read(kDummyRequestSize);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadsOneInternalReadSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.Read(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer.substr(4, 2));
  }
  {
    auto promise = promise_endpoint_.Read(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer.substr(6));
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadMultipleInternalReadsSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1), /*ready=*/true);
  }

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(kBuffer.size());
  auto promise = promise_endpoint_.Read(kBuffer.size());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadMultipleInternalReadsFailed) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1), /*ready=*/true);
  }
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);
  const size_t kReadTaskSize = kBuffer.size() + 1u;

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(kReadTaskSize);
  auto promise = promise_endpoint_.Read(kDummyRequestSize);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadsMutipleInternalReadsSuccessful) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02};
  mock_endpoint_.ScheduleReadTask(kBuffer1, /*ready=*/true);
  const std::string kBuffer2 = {0x03};
  mock_endpoint_.ScheduleReadTask(kBuffer2, /*ready=*/true);
  const std::string kBuffer3 = {0x04, 0x05};
  mock_endpoint_.ScheduleReadTask(kBuffer3, /*ready=*/true);
  const std::string kBuffer4 = {0x06};
  mock_endpoint_.ScheduleReadTask(kBuffer4, /*ready=*/true);
  const std::string kBuffer5 = {0x07};
  mock_endpoint_.ScheduleReadTask(kBuffer5, /*ready=*/true);
  const std::string kBuffer6 = {0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer6, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(6);
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(),
              kBuffer1 + kBuffer2 + kBuffer3.substr(0, 1));
  }
  {
    auto promise = promise_endpoint_.Read(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer3.substr(1) + kBuffer4);
  }
  {
    auto promise = promise_endpoint_.Read(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer5 + kBuffer6);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       FailedInternalReadsInvalidatesPreviousReadsFromReads) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer1, /*ready=*/true);
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);
  const std::string kBuffer2 = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  mock_endpoint_.ScheduleReadTask(kBuffer2, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(3);
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer1.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.Read(kDummyRequestSize);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_FALSE(poll.value().ok());
    EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  }
  // What remains from `kBuffer1` will be invalidated since there is an error
  // after it.
  {
    auto promise = promise_endpoint_.Read(kBuffer2.size());
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer2);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadAndWaitSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  mock_endpoint_.ScheduleReadTask(kBuffer);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.Read(kBuffer.size());
  EXPECT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextReadReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadAndWaitFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.Read(kDummyRequestSize);
  EXPECT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextReadReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadMultipleInternalReadsAndWaitSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1));
  }

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read).Times(kBuffer.size());
  auto promise = promise_endpoint_.Read(kBuffer.size());
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    EXPECT_TRUE(promise().pending());
    mock_endpoint_.MarkNextReadReady();
  }
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadMultipleInternalReadsAndWaitFailed) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1));
  }
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);
  const size_t kReadTaskSize = kBuffer.size() + 1u;

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read).Times(kReadTaskSize);
  auto promise = promise_endpoint_.Read(kDummyRequestSize);
  for (size_t i = 0; i < kReadTaskSize; ++i) {
    EXPECT_TRUE(promise().pending());
    mock_endpoint_.MarkNextReadReady();
  }
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       FailedInternalReadsInvalidatesPreviousReadsFromReadsWait) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer1);
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);
  const std::string kBuffer2 = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  mock_endpoint_.ScheduleReadTask(kBuffer2);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(3);
  {
    auto promise = promise_endpoint_.Read(4u);
    EXPECT_TRUE(promise().pending());
    EXPECT_CALL(activity, WakeupRequested).Times(1);
    mock_endpoint_.MarkNextReadReady();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer1.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.Read(kDummyRequestSize);
    EXPECT_TRUE(promise().pending());
    EXPECT_CALL(activity, WakeupRequested).Times(1);
    mock_endpoint_.MarkNextReadReady();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_FALSE(poll.value().ok());
    EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  }
  // What remains from `kBuffer1` will be invalidated since there is an error
  // after it.
  {
    auto promise = promise_endpoint_.Read(kBuffer2.size());
    EXPECT_TRUE(promise().pending());
    EXPECT_CALL(activity, WakeupRequested).Times(1);
    mock_endpoint_.MarkNextReadReady();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer2);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadSlice(kBuffer.size());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->as_string_view(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadSlicesOneInternalReadSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  {
    auto promise = promise_endpoint_.ReadSlice(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer.substr(4, 2));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer.substr(6));
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceMultipleInternalReadsSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1), /*ready=*/true);
  }

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(kBuffer.size());
  auto promise = promise_endpoint_.ReadSlice(kBuffer.size());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->as_string_view(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceMultipleInternalReadsFailed) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1), /*ready=*/true);
  }
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);
  const size_t kReadTaskSize = kBuffer.size() + 1u;

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(kReadTaskSize);
  auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadSlicesMutipleInternalReadsSuccessful) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02};
  mock_endpoint_.ScheduleReadTask(kBuffer1, /*ready=*/true);
  const std::string kBuffer2 = {0x03};
  mock_endpoint_.ScheduleReadTask(kBuffer2, /*ready=*/true);
  const std::string kBuffer3 = {0x04, 0x05};
  mock_endpoint_.ScheduleReadTask(kBuffer3, /*ready=*/true);
  const std::string kBuffer4 = {0x06};
  mock_endpoint_.ScheduleReadTask(kBuffer4, /*ready=*/true);
  const std::string kBuffer5 = {0x07};
  mock_endpoint_.ScheduleReadTask(kBuffer5, /*ready=*/true);
  const std::string kBuffer6 = {0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer6, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(6);
  {
    auto promise = promise_endpoint_.ReadSlice(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(),
              kBuffer1 + kBuffer2 + kBuffer3.substr(0, 1));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer3.substr(1) + kBuffer4);
  }
  {
    auto promise = promise_endpoint_.ReadSlice(2u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer5 + kBuffer6);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       FailedInternalReadsInvalidatesPreviousReadsFromReadSlices) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer1, /*ready=*/true);
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);
  const std::string kBuffer2 = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  mock_endpoint_.ScheduleReadTask(kBuffer2, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(3);
  {
    auto promise = promise_endpoint_.ReadSlice(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer1.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_FALSE(poll.value().ok());
    EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  }
  // What remains from `kBuffer1` will be invalidated since there is an error
  // after it.
  {
    auto promise = promise_endpoint_.ReadSlice(kBuffer2.size());
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer2);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceAndWaitSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  mock_endpoint_.ScheduleReadTask(kBuffer);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadSlice(kBuffer.size());
  EXPECT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextReadReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->as_string_view(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceAndWaitFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
  EXPECT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextReadReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       OneReadSliceMultipleInternalReadsAndWaitSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1));
  }

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read).Times(kBuffer.size());
  auto promise = promise_endpoint_.ReadSlice(kBuffer.size());
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    EXPECT_TRUE(promise().pending());
    mock_endpoint_.MarkNextReadReady();
  }
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(poll.value()->as_string_view(), kBuffer);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadSliceMultipleInternalReadsAndWaitFailed) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    mock_endpoint_.ScheduleReadTask(kBuffer.substr(i, 1));
  }
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);
  const size_t kReadTaskSize = kBuffer.size() + 1u;

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(1);
  EXPECT_CALL(mock_endpoint_, Read).Times(kReadTaskSize);
  auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
  for (size_t i = 0; i < kReadTaskSize; ++i) {
    EXPECT_TRUE(promise().pending());
    mock_endpoint_.MarkNextReadReady();
  }
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       FailedInternalReadsInvalidatesPreviousReadsFromReadSlicesWait) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer1);
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);
  const std::string kBuffer2 = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
  mock_endpoint_.ScheduleReadTask(kBuffer2);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(3);
  {
    auto promise = promise_endpoint_.ReadSlice(4u);
    EXPECT_TRUE(promise().pending());
    EXPECT_CALL(activity, WakeupRequested).Times(1);
    mock_endpoint_.MarkNextReadReady();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer1.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(kDummyRequestSize);
    EXPECT_TRUE(promise().pending());
    EXPECT_CALL(activity, WakeupRequested).Times(1);
    mock_endpoint_.MarkNextReadReady();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_FALSE(poll.value().ok());
    EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  }
  // What remains from `kBuffer1` will be invalidated since there is an error
  // after it.
  {
    auto promise = promise_endpoint_.ReadSlice(kBuffer2.size());
    EXPECT_TRUE(promise().pending());
    EXPECT_CALL(activity, WakeupRequested).Times(1);
    mock_endpoint_.MarkNextReadReady();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer2);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadByteSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01};
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadByte();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(*poll.value(), kBuffer[0]);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadByteFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadByte();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, MutipleReadBytesOneInternalReadSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  for (size_t i = 0; i < kBuffer.size(); ++i) {
    auto promise = promise_endpoint_.ReadByte();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(*poll.value(), kBuffer[i]);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadByteAndWaitSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01};
  mock_endpoint_.ScheduleReadTask(kBuffer);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadByte();
  ASSERT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextReadReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_TRUE(poll.value().ok());
  EXPECT_EQ(*poll.value(), kBuffer[0]);
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       MutipleReadsReadSlicesReadBytesOneInternalReadSuccessful) {
  MockActivity activity;
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(), kBuffer.substr(0, 4));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(3u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(), kBuffer.substr(4, 3));
  }
  {
    auto promise = promise_endpoint_.ReadByte();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(*poll.value(), kBuffer[7]);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest,
       MutipleReadsReadSlicesReadBytesMutipleInternalReadsSuccessful) {
  MockActivity activity;
  const std::string kBuffer1 = {0x01, 0x02};
  mock_endpoint_.ScheduleReadTask(kBuffer1, /*ready=*/true);
  const std::string kBuffer2 = {0x03};
  mock_endpoint_.ScheduleReadTask(kBuffer2, /*ready=*/true);
  const std::string kBuffer3 = {0x04, 0x05};
  mock_endpoint_.ScheduleReadTask(kBuffer3, /*ready=*/true);
  const std::string kBuffer4 = {0x06};
  mock_endpoint_.ScheduleReadTask(kBuffer4, /*ready=*/true);
  const std::string kBuffer5 = {0x07};
  mock_endpoint_.ScheduleReadTask(kBuffer5, /*ready=*/true);
  const std::string kBuffer6 = {0x08};
  mock_endpoint_.ScheduleReadTask(kBuffer6, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(6);
  {
    auto promise = promise_endpoint_.Read(4u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->JoinIntoString(),
              kBuffer1 + kBuffer2 + kBuffer3.substr(0, 1));
  }
  {
    auto promise = promise_endpoint_.ReadSlice(3u);
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(poll.value()->as_string_view(),
              kBuffer3.substr(1) + kBuffer4 + kBuffer5);
  }
  {
    auto promise = promise_endpoint_.ReadByte();
    auto poll = promise();
    ASSERT_TRUE(poll.ready());
    ASSERT_TRUE(poll.value().ok());
    EXPECT_EQ(*poll.value(), kBuffer6[0]);
  }
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneReadByteAndWaitFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleReadTask(kDummyErrorStatus);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Read).Times(1);
  auto promise = promise_endpoint_.ReadByte();
  ASSERT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextReadReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  ASSERT_FALSE(poll.value().ok());
  EXPECT_EQ(kDummyErrorStatus, poll.value().status());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneWriteSuccessful) {
  MockActivity activity;
  mock_endpoint_.ScheduleWriteTask(absl::OkStatus(), /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Write).Times(1);
  auto promise = promise_endpoint_.Write(SliceBuffer());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(absl::OkStatus(), poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, OneWriteFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleWriteTask(kDummyErrorStatus, /*ready=*/true);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Write).Times(1);
  auto promise = promise_endpoint_.Write(SliceBuffer());
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(kDummyErrorStatus, poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, WriteAndWaitSuccessful) {
  MockActivity activity;
  mock_endpoint_.ScheduleWriteTask(absl::OkStatus());

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Write).Times(1);
  auto promise = promise_endpoint_.Write(SliceBuffer());
  EXPECT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextWriteReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(absl::OkStatus(), poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, WriteAndWaitFailed) {
  MockActivity activity;
  mock_endpoint_.ScheduleWriteTask(kDummyErrorStatus);

  activity.Activate();
  EXPECT_CALL(activity, WakeupRequested).Times(0);
  EXPECT_CALL(mock_endpoint_, Write).Times(1);
  auto promise = promise_endpoint_.Write(SliceBuffer());
  EXPECT_TRUE(promise().pending());

  EXPECT_CALL(activity, WakeupRequested).Times(1);
  mock_endpoint_.MarkNextWriteReady();
  auto poll = promise();
  ASSERT_TRUE(poll.ready());
  EXPECT_EQ(kDummyErrorStatus, poll.value());
  activity.Deactivate();
}

TEST_F(PromiseEndpointTest, GetPeerAddress) {
  // Just some random bytes.
  const char raw_peer_address[] = {0x55, 0x66, 0x01, 0x55, 0x66, 0x01};
  grpc_event_engine::experimental::EventEngine::ResolvedAddress peer_address(
      reinterpret_cast<const sockaddr*>(raw_peer_address),
      sizeof(raw_peer_address));
  mock_endpoint_.set_peer_address(peer_address);

  EXPECT_CALL(mock_endpoint_, GetPeerAddress).Times(1);
  EXPECT_EQ(0, std::memcmp(promise_endpoint_.GetPeerAddress().address(),
                           peer_address.address(),
                           grpc_event_engine::experimental::EventEngine::
                               ResolvedAddress::MAX_SIZE_BYTES));
  EXPECT_CALL(mock_endpoint_, GetPeerAddress).Times(1);
  EXPECT_EQ(peer_address.size(), promise_endpoint_.GetPeerAddress().size());
}

TEST_F(PromiseEndpointTest, GetLocalAddress) {
  // Just some random bytes.
  const char raw_local_address[] = {0x52, 0x55, 0x66, 0x52, 0x55, 0x66};
  grpc_event_engine::experimental::EventEngine::ResolvedAddress local_address(
      reinterpret_cast<const sockaddr*>(raw_local_address),
      sizeof(raw_local_address));
  mock_endpoint_.set_local_address(local_address);

  EXPECT_CALL(mock_endpoint_, GetLocalAddress).Times(1);
  EXPECT_EQ(0, std::memcmp(promise_endpoint_.GetLocalAddress().address(),
                           local_address.address(),
                           grpc_event_engine::experimental::EventEngine::
                               ResolvedAddress::MAX_SIZE_BYTES));
  EXPECT_CALL(mock_endpoint_, GetLocalAddress).Times(1);
  EXPECT_EQ(local_address.size(), promise_endpoint_.GetLocalAddress().size());
}

class MultiplePromiseEndpointTest : public ::testing::Test {
 public:
  MultiplePromiseEndpointTest()
      : first_mock_endpoint_ptr_(new ::testing::NiceMock<MockEndpoint>()),
        second_mock_endpoint_ptr_(new ::testing::NiceMock<MockEndpoint>()),
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
  first_mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);
  second_mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);

  EXPECT_CALL(first_mock_endpoint_, Read).Times(1);
  EXPECT_CALL(second_mock_endpoint_, Read).Times(1);

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
};

TEST_F(MultiplePromiseEndpointTest, JoinOneReadSuccessfulOneReadFailed) {
  const std::string kBuffer = {0x01, 0x02, 0x03, 0x04};
  first_mock_endpoint_.ScheduleReadTask(kBuffer, /*ready=*/true);
  second_mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);

  EXPECT_CALL(first_mock_endpoint_, Read).Times(1);
  EXPECT_CALL(second_mock_endpoint_, Read).Times(1);

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
};

TEST_F(MultiplePromiseEndpointTest, JoinReadsFailed) {
  first_mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);
  second_mock_endpoint_.ScheduleReadTask(kDummyErrorStatus, /*ready=*/true);

  EXPECT_CALL(first_mock_endpoint_, Read).Times(1);
  EXPECT_CALL(second_mock_endpoint_, Read).Times(1);

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
};

TEST_F(MultiplePromiseEndpointTest, JoinWritesSuccessful) {
  first_mock_endpoint_.ScheduleWriteTask(absl::OkStatus(), /*ready=*/true);
  second_mock_endpoint_.ScheduleWriteTask(absl::OkStatus(), /*ready=*/true);

  EXPECT_CALL(first_mock_endpoint_, Write).Times(1);
  EXPECT_CALL(second_mock_endpoint_, Write).Times(1);

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
};

TEST_F(MultiplePromiseEndpointTest, JoinOneWriteSuccessfulOneWriteFailed) {
  first_mock_endpoint_.ScheduleWriteTask(absl::OkStatus(), /*ready=*/true);
  second_mock_endpoint_.ScheduleWriteTask(kDummyErrorStatus, /*ready=*/true);

  EXPECT_CALL(first_mock_endpoint_, Write).Times(1);
  EXPECT_CALL(second_mock_endpoint_, Write).Times(1);

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
};

TEST_F(MultiplePromiseEndpointTest, JoinWritesFailed) {
  first_mock_endpoint_.ScheduleWriteTask(kDummyErrorStatus, /*ready=*/true);
  second_mock_endpoint_.ScheduleWriteTask(kDummyErrorStatus, /*ready=*/true);

  EXPECT_CALL(first_mock_endpoint_, Write).Times(1);
  EXPECT_CALL(second_mock_endpoint_, Write).Times(1);

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
};

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
