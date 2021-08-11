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

// Unit tests for WireReaderImpl.
//
// WireReaderImpl is responsible for turning incoming transactions into
// top-level metadata. The following tests verify that the interactions between
// WireReaderImpl and both the output (readable) parcel and the transport stream
// receiver are correct in all possible situations.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"
#include "test/core/transport/binder/mock_objects.h"
#include "test/core/util/test_config.h"

namespace grpc_binder {

using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

namespace {

class WireReaderTest : public ::testing::Test {
 public:
  WireReaderTest()
      : transport_stream_receiver_(
            std::make_shared<StrictMock<MockTransportStreamReceiver>>()),
        wire_reader_(transport_stream_receiver_, /*is_client=*/true) {}

 protected:
  void ExpectReadInt32(int result) {
    EXPECT_CALL(mock_readable_parcel_, ReadInt32)
        .WillOnce(DoAll(SetArgPointee<0>(result), Return(absl::OkStatus())));
  }

  void ExpectReadByteArray(const std::string& buffer) {
    ExpectReadInt32(buffer.length());
    if (!buffer.empty()) {
      EXPECT_CALL(mock_readable_parcel_, ReadByteArray)
          .WillOnce([buffer](std::string* data) {
            *data = buffer;
            return absl::OkStatus();
          });
    }
  }

  template <typename T>
  absl::Status CallProcessTransaction(T tx_code) {
    return wire_reader_.ProcessTransaction(
        static_cast<transaction_code_t>(tx_code), &mock_readable_parcel_);
  }

  std::shared_ptr<StrictMock<MockTransportStreamReceiver>>
      transport_stream_receiver_;
  WireReaderImpl wire_reader_;
  StrictMock<MockReadableParcel> mock_readable_parcel_;
};

MATCHER_P(StatusOrStrEq, target, "") {
  if (!arg.ok()) return false;
  return arg.value() == target;
}

MATCHER_P(StatusOrContainerEq, target, "") {
  if (!arg.ok()) return false;
  return arg.value() == target;
}

}  // namespace

TEST_F(WireReaderTest, SetupTransport) {
  auto mock_binder = absl::make_unique<MockBinder>();
  MockBinder& mock_binder_ref = *mock_binder;

  ::testing::InSequence sequence;
  EXPECT_CALL(mock_binder_ref, Initialize);
  EXPECT_CALL(mock_binder_ref, PrepareTransaction);
  const MockReadableParcel mock_readable_parcel;
  EXPECT_CALL(mock_binder_ref, GetWritableParcel);

  // Write version.
  EXPECT_CALL(mock_binder_ref.GetWriter(), WriteInt32(77));

  // The transaction receiver immediately informs the wire writer that the
  // transport has been successfully set up.
  EXPECT_CALL(mock_binder_ref, ConstructTxReceiver);

  EXPECT_CALL(mock_binder_ref.GetReader(), ReadInt32);
  EXPECT_CALL(mock_binder_ref.GetReader(), ReadBinder);

  // Write transaction receiver.
  EXPECT_CALL(mock_binder_ref.GetWriter(), WriteBinder);
  // Perform transaction.
  EXPECT_CALL(mock_binder_ref, Transact);

  wire_reader_.SetupTransport(std::move(mock_binder));
}

TEST_F(WireReaderTest, ProcessTransactionControlMessageSetupTransport) {
  ::testing::InSequence sequence;

  EXPECT_CALL(mock_readable_parcel_, ReadInt32);
  EXPECT_CALL(mock_readable_parcel_, ReadBinder)
      .WillOnce([&](std::unique_ptr<Binder>* binder) {
        auto mock_binder = absl::make_unique<MockBinder>();
        // binder that is read from the output parcel must first be initialized
        // before it can be used.
        EXPECT_CALL(*mock_binder, Initialize);
        *binder = std::move(mock_binder);
        return absl::OkStatus();
      });

  EXPECT_TRUE(
      CallProcessTransaction(BinderTransportTxCode::SETUP_TRANSPORT).ok());
}

TEST_F(WireReaderTest, ProcessTransactionControlMessagePingResponse) {
  EXPECT_CALL(mock_readable_parcel_, ReadInt32);
  EXPECT_TRUE(
      CallProcessTransaction(BinderTransportTxCode::PING_RESPONSE).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataEmptyFlagIgnored) {
  ::testing::InSequence sequence;

  // first transaction: empty flag
  ExpectReadInt32(0);
  // Won't further read sequence number.
  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest,
       ProcessTransactionServerRpcDataFlagPrefixWithoutMetadata) {
  ::testing::InSequence sequence;

  // flag
  ExpectReadInt32(kFlagPrefix);
  // sequence number
  ExpectReadInt32(0);

  // count
  ExpectReadInt32(0);
  EXPECT_CALL(
      *transport_stream_receiver_,
      NotifyRecvInitialMetadata(kFirstCallId, StatusOrContainerEq(Metadata{})));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataFlagPrefixWithMetadata) {
  ::testing::InSequence sequence;

  // flag
  ExpectReadInt32(kFlagPrefix);
  // sequence number
  ExpectReadInt32(0);

  const std::vector<std::pair<std::string, std::string>> kMetadata = {
      {"", ""},
      {"", "value"},
      {"key", ""},
      {"key", "value"},
      {"another-key", "another-value"},
  };

  // count
  ExpectReadInt32(kMetadata.size());
  for (const auto& md : kMetadata) {
    // metadata key
    ExpectReadByteArray(md.first);
    // metadata val
    // TODO(waynetu): metadata value can also be "parcelable".
    ExpectReadByteArray(md.second);
  }
  EXPECT_CALL(
      *transport_stream_receiver_,
      NotifyRecvInitialMetadata(kFirstCallId, StatusOrContainerEq(kMetadata)));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataFlagMessageDataNonEmpty) {
  ::testing::InSequence sequence;

  // flag
  ExpectReadInt32(kFlagMessageData);
  // sequence number
  ExpectReadInt32(0);

  // message data
  // TODO(waynetu): message data can also be "parcelable".
  const std::string kMessageData = "message data";
  ExpectReadByteArray(kMessageData);
  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvMessage(kFirstCallId, StatusOrStrEq(kMessageData)));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataFlagMessageDataEmpty) {
  ::testing::InSequence sequence;

  // flag
  ExpectReadInt32(kFlagMessageData);
  // sequence number
  ExpectReadInt32(0);

  // message data
  // TODO(waynetu): message data can also be "parcelable".
  const std::string kMessageData = "";
  ExpectReadByteArray(kMessageData);
  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvMessage(kFirstCallId, StatusOrStrEq(kMessageData)));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataFlagSuffixWithStatus) {
  ::testing::InSequence sequence;

  constexpr int kStatus = 0x1234;
  // flag
  ExpectReadInt32(kFlagSuffix | kFlagStatusDescription | (kStatus << 16));
  // sequence number
  ExpectReadInt32(0);
  // status description
  EXPECT_CALL(mock_readable_parcel_, ReadString);
  // metadata count
  ExpectReadInt32(0);
  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvTrailingMetadata(
                  kFirstCallId, StatusOrContainerEq(Metadata{}), kStatus));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataFlagSuffixWithoutStatus) {
  ::testing::InSequence sequence;

  // flag
  ExpectReadInt32(kFlagSuffix);
  // sequence number
  ExpectReadInt32(0);
  // No status description
  // metadata count
  ExpectReadInt32(0);
  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvTrailingMetadata(kFirstCallId,
                                         StatusOrContainerEq(Metadata{}), 0));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
