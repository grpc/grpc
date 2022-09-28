// Copyright 2021 gRPC authors.
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

#include <cassert>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.h"
#include "test/core/util/test_config.h"

namespace grpc_binder {
namespace {

// TODO(waynetu): These are hacks to make callbacks aware of their stream IDs
// and sequence numbers. Remove/Refactor these hacks when possible.
template <typename T>
std::pair<StreamIdentifier, int> Decode(const T& /*data*/) {
  assert(false && "This should not be called");
  return {};
}

template <>
std::pair<StreamIdentifier, int> Decode<std::string>(const std::string& data) {
  assert(data.size() == sizeof(StreamIdentifier) + sizeof(int));
  StreamIdentifier id{};
  int seq_num{};
  std::memcpy(&id, data.data(), sizeof(StreamIdentifier));
  std::memcpy(&seq_num, data.data() + sizeof(StreamIdentifier), sizeof(int));
  return std::make_pair(id, seq_num);
}

template <>
std::pair<StreamIdentifier, int> Decode<Metadata>(const Metadata& data) {
  assert(data.size() == 1);
  const std::string& encoding = data[0].first;
  return Decode(encoding);
}

template <typename T>
T Encode(StreamIdentifier /*id*/, int /*seq_num*/) {
  assert(false && "This should not be called");
  return {};
}

template <>
std::string Encode<std::string>(StreamIdentifier id, int seq_num) {
  char result[sizeof(StreamIdentifier) + sizeof(int)];
  std::memcpy(result, &id, sizeof(StreamIdentifier));
  std::memcpy(result + sizeof(StreamIdentifier), &seq_num, sizeof(int));
  return std::string(result, sizeof(StreamIdentifier) + sizeof(int));
}

template <>
Metadata Encode<Metadata>(StreamIdentifier id, int seq_num) {
  return {{Encode<std::string>(id, seq_num), ""}};
}

MATCHER_P2(StreamIdAndSeqNumMatch, id, seq_num, "") {
  auto p = Decode(arg.value());
  return p.first == id && p.second == seq_num;
}

// MockCallback is used to verify the every callback passed to transaction
// receiver will eventually be invoked with the artifact of its corresponding
// binder transaction.
template <typename FirstArg, typename... TrailingArgs>
class MockCallback {
 public:
  explicit MockCallback(StreamIdentifier id, int seq_num)
      : id_(id), seq_num_(seq_num) {}

  MOCK_METHOD(void, ActualCallback, (FirstArg), ());

  std::function<void(FirstArg, TrailingArgs...)> GetHandle() {
    return [this](FirstArg first_arg, TrailingArgs...) {
      this->ActualCallback(first_arg);
    };
  }

  void ExpectCallbackInvocation() {
    EXPECT_CALL(*this, ActualCallback(StreamIdAndSeqNumMatch(id_, seq_num_)));
  }

 private:
  StreamIdentifier id_;
  int seq_num_;
};

using MockInitialMetadataCallback = MockCallback<absl::StatusOr<Metadata>>;
using MockMessageCallback = MockCallback<absl::StatusOr<std::string>>;
using MockTrailingMetadataCallback =
    MockCallback<absl::StatusOr<Metadata>, int>;

class MockOpBatch {
 public:
  MockOpBatch(StreamIdentifier id, int flag, int seq_num)
      : id_(id), flag_(flag), seq_num_(seq_num) {
    if (flag_ & kFlagPrefix) {
      initial_metadata_callback_ =
          std::make_unique<MockInitialMetadataCallback>(id_, seq_num_);
    }
    if (flag_ & kFlagMessageData) {
      message_callback_ = std::make_unique<MockMessageCallback>(id_, seq_num_);
    }
    if (flag_ & kFlagSuffix) {
      trailing_metadata_callback_ =
          std::make_unique<MockTrailingMetadataCallback>(id_, seq_num_);
    }
  }

  void Complete(TransportStreamReceiver& receiver) {
    if (flag_ & kFlagPrefix) {
      initial_metadata_callback_->ExpectCallbackInvocation();
      receiver.NotifyRecvInitialMetadata(id_, Encode<Metadata>(id_, seq_num_));
    }
    if (flag_ & kFlagMessageData) {
      message_callback_->ExpectCallbackInvocation();
      receiver.NotifyRecvMessage(id_, Encode<std::string>(id_, seq_num_));
    }
    if (flag_ & kFlagSuffix) {
      trailing_metadata_callback_->ExpectCallbackInvocation();
      receiver.NotifyRecvTrailingMetadata(id_, Encode<Metadata>(id_, seq_num_),
                                          0);
    }
  }

  void RequestRecv(TransportStreamReceiver& receiver) {
    if (flag_ & kFlagPrefix) {
      receiver.RegisterRecvInitialMetadata(
          id_, initial_metadata_callback_->GetHandle());
    }
    if (flag_ & kFlagMessageData) {
      receiver.RegisterRecvMessage(id_, message_callback_->GetHandle());
    }
    if (flag_ & kFlagSuffix) {
      receiver.RegisterRecvTrailingMetadata(
          id_, trailing_metadata_callback_->GetHandle());
    }
  }

  MockOpBatch NextBatch(int flag) const {
    return MockOpBatch(id_, flag, seq_num_ + 1);
  }

 private:
  std::unique_ptr<MockInitialMetadataCallback> initial_metadata_callback_;
  std::unique_ptr<MockMessageCallback> message_callback_;
  std::unique_ptr<MockTrailingMetadataCallback> trailing_metadata_callback_;
  int id_, flag_, seq_num_;
};

class TransportStreamReceiverTest : public ::testing::Test {
 protected:
  MockOpBatch NewGrpcStream(int flag) {
    return MockOpBatch(current_id_++, flag, 0);
  }

  StreamIdentifier current_id_ = 0;
};

const int kFlagAll = kFlagPrefix | kFlagMessageData | kFlagSuffix;

}  // namespace

TEST_F(TransportStreamReceiverTest, MultipleStreamRequestThenComplete) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagAll);
  t0.RequestRecv(receiver);
  t0.Complete(receiver);
}

TEST_F(TransportStreamReceiverTest, MultipleStreamCompleteThenRequest) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagAll);
  t0.Complete(receiver);
  t0.RequestRecv(receiver);
}

TEST_F(TransportStreamReceiverTest, MultipleStreamInterleaved) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagAll);
  MockOpBatch t1 = NewGrpcStream(kFlagAll);
  t1.Complete(receiver);
  t0.Complete(receiver);
  t0.RequestRecv(receiver);
  t1.RequestRecv(receiver);
}

TEST_F(TransportStreamReceiverTest, MultipleStreamInterleavedReversed) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagAll);
  MockOpBatch t1 = NewGrpcStream(kFlagAll);
  t0.RequestRecv(receiver);
  t1.RequestRecv(receiver);
  t1.Complete(receiver);
  t0.Complete(receiver);
}

TEST_F(TransportStreamReceiverTest, MultipleStreamMoreInterleaved) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagAll);
  MockOpBatch t1 = NewGrpcStream(kFlagAll);
  t0.RequestRecv(receiver);
  t1.Complete(receiver);
  MockOpBatch t2 = NewGrpcStream(kFlagAll);
  t2.RequestRecv(receiver);
  t0.Complete(receiver);
  t1.RequestRecv(receiver);
  t2.Complete(receiver);
}

TEST_F(TransportStreamReceiverTest, SingleStreamUnaryCall) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagPrefix);
  MockOpBatch t1 = t0.NextBatch(kFlagMessageData);
  MockOpBatch t2 = t1.NextBatch(kFlagSuffix);
  t0.RequestRecv(receiver);
  t1.RequestRecv(receiver);
  t2.RequestRecv(receiver);
  t0.Complete(receiver);
  t1.Complete(receiver);
  t2.Complete(receiver);
}

TEST_F(TransportStreamReceiverTest, SingleStreamStreamingCall) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagPrefix);
  t0.RequestRecv(receiver);
  t0.Complete(receiver);
  MockOpBatch t1 = t0.NextBatch(kFlagMessageData);
  t1.Complete(receiver);
  t1.RequestRecv(receiver);
  MockOpBatch t2 = t1.NextBatch(kFlagMessageData);
  t2.RequestRecv(receiver);
  t2.Complete(receiver);
  MockOpBatch t3 = t2.NextBatch(kFlagMessageData);
  MockOpBatch t4 = t3.NextBatch(kFlagMessageData);
  t3.Complete(receiver);
  t4.Complete(receiver);
  t3.RequestRecv(receiver);
  t4.RequestRecv(receiver);
}

TEST_F(TransportStreamReceiverTest, DISABLED_SingleStreamBufferedCallbacks) {
  TransportStreamReceiverImpl receiver(/*is_client=*/true);
  MockOpBatch t0 = NewGrpcStream(kFlagPrefix);
  MockOpBatch t1 = t0.NextBatch(kFlagMessageData);
  MockOpBatch t2 = t1.NextBatch(kFlagMessageData);
  MockOpBatch t3 = t2.NextBatch(kFlagSuffix);
  t0.RequestRecv(receiver);
  // TODO(waynetu): Can gRPC issues recv_message before it actually receives the
  // previous one?
  t1.RequestRecv(receiver);
  t2.RequestRecv(receiver);
  t3.RequestRecv(receiver);
  t0.Complete(receiver);
  t1.Complete(receiver);
  t2.Complete(receiver);
  t3.Complete(receiver);
}

// TODO(waynetu): Should we have some concurrent stress tests to make sure that
// thread safety is well taken care of?

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
