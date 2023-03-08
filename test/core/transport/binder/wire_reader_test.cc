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

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpcpp/security/binder_security_policy.h>

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
 protected:
  void SetUp() override { SetUp(true); }
  void SetUp(bool is_client) {
    transport_stream_receiver_ =
        std::make_shared<StrictMock<MockTransportStreamReceiver>>();
    wire_reader_ = std::make_shared<WireReaderImpl>(
        transport_stream_receiver_, is_client,
        std::make_shared<
            grpc::experimental::binder::UntrustedSecurityPolicy>());
  }

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

  void ExpectReadString(const std::string& str) {
    EXPECT_CALL(mock_readable_parcel_, ReadString)
        .WillOnce([str](std::string* out) {
          *out = str;
          return absl::OkStatus();
        });
  }

  void UnblockSetupTransport() {
    // SETUP_TRANSPORT should finish before we can proceed with any other
    // requests and streaming calls. The MockBinder will construct a
    // MockTransactionReceiver, which will then sends SETUP_TRANSPORT request
    // back to us.
    wire_reader_->SetupTransport(std::make_unique<MockBinder>());
  }

  template <typename T>
  absl::Status CallProcessTransaction(T tx_code) {
    return wire_reader_->ProcessTransaction(
        static_cast<transaction_code_t>(tx_code), &mock_readable_parcel_,
        /*uid=*/0);
  }

  std::shared_ptr<StrictMock<MockTransportStreamReceiver>>
      transport_stream_receiver_;
  std::shared_ptr<WireReaderImpl> wire_reader_;
  MockReadableParcel mock_readable_parcel_;
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
  auto mock_binder = std::make_unique<MockBinder>();
  MockBinder& mock_binder_ref = *mock_binder;

  ::testing::InSequence sequence;
  EXPECT_CALL(mock_binder_ref, Initialize);
  EXPECT_CALL(mock_binder_ref, PrepareTransaction);
  const MockReadableParcel mock_readable_parcel;
  EXPECT_CALL(mock_binder_ref, GetWritableParcel);

  // Write version.
  EXPECT_CALL(mock_binder_ref.GetWriter(), WriteInt32(1));

  wire_reader_->SetupTransport(std::move(mock_binder));
}

TEST_F(WireReaderTest, ProcessTransactionControlMessageSetupTransport) {
  ::testing::InSequence sequence;
  UnblockSetupTransport();
}

TEST_F(WireReaderTest, ProcessTransactionControlMessagePingResponse) {
  ::testing::InSequence sequence;
  UnblockSetupTransport();
  EXPECT_CALL(mock_readable_parcel_, ReadInt32);
  EXPECT_TRUE(
      CallProcessTransaction(BinderTransportTxCode::PING_RESPONSE).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataEmptyFlagIgnored) {
  ::testing::InSequence sequence;
  UnblockSetupTransport();

  // first transaction: empty flag
  ExpectReadInt32(0);
  // Won't further read sequence number.
  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest,
       ProcessTransactionServerRpcDataFlagPrefixWithoutMetadata) {
  ::testing::InSequence sequence;
  UnblockSetupTransport();

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
  UnblockSetupTransport();

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
  UnblockSetupTransport();

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
  UnblockSetupTransport();

  // flag
  ExpectReadInt32(kFlagMessageData);
  // sequence number
  ExpectReadInt32(0);

  // message data
  // TODO(waynetu): message data can also be "parcelable".
  const std::string kMessageData;
  ExpectReadByteArray(kMessageData);
  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvMessage(kFirstCallId, StatusOrStrEq(kMessageData)));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ProcessTransactionServerRpcDataFlagSuffixWithStatus) {
  ::testing::InSequence sequence;
  UnblockSetupTransport();

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
  UnblockSetupTransport();

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

TEST_F(WireReaderTest, InBoundFlowControl) {
  ::testing::InSequence sequence;
  UnblockSetupTransport();

  // data size
  EXPECT_CALL(mock_readable_parcel_, GetDataSize).WillOnce(Return(1000));
  // flag
  ExpectReadInt32(kFlagMessageData | kFlagMessageDataIsPartial);
  // sequence number
  ExpectReadInt32(0);
  // message size
  ExpectReadInt32(1000);
  EXPECT_CALL(mock_readable_parcel_, ReadByteArray)
      .WillOnce(DoAll(SetArgPointee<0>(std::string(1000, 'a')),
                      Return(absl::OkStatus())));

  // Data is not completed. No callback will be triggered.
  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());

  EXPECT_CALL(mock_readable_parcel_, GetDataSize).WillOnce(Return(1000));
  // flag
  ExpectReadInt32(kFlagMessageData);
  // sequence number
  ExpectReadInt32(1);
  // message size
  ExpectReadInt32(1000);
  EXPECT_CALL(mock_readable_parcel_, ReadByteArray)
      .WillOnce(DoAll(SetArgPointee<0>(std::string(1000, 'b')),
                      Return(absl::OkStatus())));

  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvMessage(kFirstCallId,
                                StatusOrContainerEq(std::string(1000, 'a') +
                                                    std::string(1000, 'b'))));
  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

TEST_F(WireReaderTest, ServerInitialMetadata) {
  SetUp(/*is_client=*/false);

  ::testing::InSequence sequence;
  UnblockSetupTransport();

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

  // method ref
  ExpectReadString("test.service/rpc.method");

  // metadata
  {
    // count
    ExpectReadInt32(kMetadata.size());
    for (const auto& md : kMetadata) {
      // metadata key
      ExpectReadByteArray(md.first);
      // metadata val
      // TODO(waynetu): metadata value can also be "parcelable".
      ExpectReadByteArray(md.second);
    }
  }

  // Since path and authority is not encoded as metadata in wire format,
  // wire_reader implementation should insert them as metadata before passing
  // to transport layer.
  auto metadata_expectation = kMetadata;
  metadata_expectation.push_back({":path", "/test.service/rpc.method"});
  metadata_expectation.push_back({":authority", "binder.authority"});

  EXPECT_CALL(*transport_stream_receiver_,
              NotifyRecvInitialMetadata(
                  kFirstCallId, StatusOrContainerEq(metadata_expectation)));

  EXPECT_TRUE(CallProcessTransaction(kFirstCallId).ok());
}

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
