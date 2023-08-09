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

#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"

#include <grpcpp/impl/grpc_library.h>

#include "test/core/transport/binder/mock_objects.h"
#include "test/core/util/test_config.h"

namespace grpc_binder {

using ::testing::Return;

MATCHER_P(StrEqInt8Ptr, target, "") {
  return std::string(reinterpret_cast<const char*>(arg), target.size()) ==
         target;
}

TEST(WireWriterTest, RpcCall) {
  grpc::internal::GrpcLibrary init_lib;
  // Required because wire writer uses combiner internally.
  grpc_core::ExecCtx exec_ctx;
  auto mock_binder = std::make_unique<MockBinder>();
  MockBinder& mock_binder_ref = *mock_binder;
  MockWritableParcel mock_writable_parcel;
  ON_CALL(mock_binder_ref, GetWritableParcel)
      .WillByDefault(Return(&mock_writable_parcel));
  WireWriterImpl wire_writer(std::move(mock_binder));

  auto ExpectWriteByteArray = [&](const std::string& target) {
    // length
    EXPECT_CALL(mock_writable_parcel, WriteInt32(target.size()));
    if (!target.empty()) {
      // content
      EXPECT_CALL(mock_writable_parcel,
                  WriteByteArray(StrEqInt8Ptr(target), target.size()));
    }
  };

  ::testing::InSequence sequence;
  int sequence_number = 0;

  {
    // flag
    EXPECT_CALL(mock_writable_parcel, WriteInt32(0));
    // sequence number
    EXPECT_CALL(mock_writable_parcel, WriteInt32(sequence_number));

    EXPECT_CALL(mock_binder_ref, Transact(BinderTransportTxCode(kFirstCallId)));

    auto tx = std::make_unique<Transaction>(kFirstCallId, /*is_client=*/true);
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    sequence_number++;
    grpc_core::ExecCtx::Get()->Flush();
  }
  {
    // flag
    EXPECT_CALL(mock_writable_parcel, WriteInt32(kFlagPrefix));
    // sequence number. This is another stream so the sequence number starts
    // with 0.
    EXPECT_CALL(mock_writable_parcel, WriteInt32(0));

    EXPECT_CALL(mock_writable_parcel,
                WriteString(absl::string_view("/example/method/ref")));

    const std::vector<std::pair<std::string, std::string>> kMetadata = {
        {"", ""},
        {"", "value"},
        {"key", ""},
        {"key", "value"},
        {"another-key", "another-value"},
    };

    // Number of metadata
    EXPECT_CALL(mock_writable_parcel, WriteInt32(kMetadata.size()));

    for (const auto& md : kMetadata) {
      ExpectWriteByteArray(md.first);
      ExpectWriteByteArray(md.second);
    }

    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 1)));

    auto tx =
        std::make_unique<Transaction>(kFirstCallId + 1, /*is_client=*/true);
    tx->SetPrefix(kMetadata);
    tx->SetMethodRef("/example/method/ref");
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    grpc_core::ExecCtx::Get()->Flush();
  }
  {
    // flag
    EXPECT_CALL(mock_writable_parcel, WriteInt32(kFlagMessageData));
    // sequence number
    EXPECT_CALL(mock_writable_parcel, WriteInt32(sequence_number));

    ExpectWriteByteArray("data");
    EXPECT_CALL(mock_binder_ref, Transact(BinderTransportTxCode(kFirstCallId)));

    auto tx = std::make_unique<Transaction>(kFirstCallId, /*is_client=*/true);
    tx->SetData("data");
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    sequence_number++;
    grpc_core::ExecCtx::Get()->Flush();
  }
  {
    // flag
    EXPECT_CALL(mock_writable_parcel, WriteInt32(kFlagSuffix));
    // sequence number
    EXPECT_CALL(mock_writable_parcel, WriteInt32(sequence_number));

    EXPECT_CALL(mock_binder_ref, Transact(BinderTransportTxCode(kFirstCallId)));

    auto tx = std::make_unique<Transaction>(kFirstCallId, /*is_client=*/true);
    tx->SetSuffix({});
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    sequence_number++;
    grpc_core::ExecCtx::Get()->Flush();
  }
  {
    // flag
    EXPECT_CALL(mock_writable_parcel,
                WriteInt32(kFlagPrefix | kFlagMessageData | kFlagSuffix));
    // sequence number
    EXPECT_CALL(mock_writable_parcel, WriteInt32(sequence_number));

    EXPECT_CALL(mock_writable_parcel,
                WriteString(absl::string_view("/example/method/ref")));

    const std::vector<std::pair<std::string, std::string>> kMetadata = {
        {"", ""},
        {"", "value"},
        {"key", ""},
        {"key", "value"},
        {"another-key", "another-value"},
    };

    // Number of metadata
    EXPECT_CALL(mock_writable_parcel, WriteInt32(kMetadata.size()));

    for (const auto& md : kMetadata) {
      ExpectWriteByteArray(md.first);
      ExpectWriteByteArray(md.second);
    }

    // Empty message data
    ExpectWriteByteArray("");

    EXPECT_CALL(mock_binder_ref, Transact(BinderTransportTxCode(kFirstCallId)));

    auto tx = std::make_unique<Transaction>(kFirstCallId, /*is_client=*/true);
    // TODO(waynetu): Implement a helper function that automatically creates
    // EXPECT_CALL based on the tx object.
    tx->SetPrefix(kMetadata);
    tx->SetMethodRef("/example/method/ref");
    tx->SetData("");
    tx->SetSuffix({});
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    sequence_number++;
    grpc_core::ExecCtx::Get()->Flush();
  }

  // Really large message
  {
    EXPECT_CALL(mock_writable_parcel,
                WriteInt32(kFlagMessageData | kFlagMessageDataIsPartial));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(0));
    ExpectWriteByteArray(std::string(WireWriterImpl::kBlockSize, 'a'));
    EXPECT_CALL(mock_writable_parcel, GetDataSize)
        .WillOnce(Return(WireWriterImpl::kBlockSize));
    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 2)));

    EXPECT_CALL(mock_writable_parcel,
                WriteInt32(kFlagMessageData | kFlagMessageDataIsPartial));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(1));
    ExpectWriteByteArray(std::string(WireWriterImpl::kBlockSize, 'a'));
    EXPECT_CALL(mock_writable_parcel, GetDataSize)
        .WillOnce(Return(WireWriterImpl::kBlockSize));
    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 2)));

    EXPECT_CALL(mock_writable_parcel, WriteInt32(kFlagMessageData));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(2));
    ExpectWriteByteArray("a");
    EXPECT_CALL(mock_writable_parcel, GetDataSize).WillOnce(Return(1));
    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 2)));

    // Use a new stream.
    auto tx =
        std::make_unique<Transaction>(kFirstCallId + 2, /*is_client=*/true);
    tx->SetData(std::string(2 * WireWriterImpl::kBlockSize + 1, 'a'));
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    grpc_core::ExecCtx::Get()->Flush();
  }
  // Really large message with metadata
  {
    EXPECT_CALL(
        mock_writable_parcel,
        WriteInt32(kFlagPrefix | kFlagMessageData | kFlagMessageDataIsPartial));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(0));
    EXPECT_CALL(mock_writable_parcel, WriteString(absl::string_view("123")));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(0));
    ExpectWriteByteArray(std::string(WireWriterImpl::kBlockSize, 'a'));
    EXPECT_CALL(mock_writable_parcel, GetDataSize)
        .WillOnce(Return(WireWriterImpl::kBlockSize));
    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 3)));

    EXPECT_CALL(mock_writable_parcel,
                WriteInt32(kFlagMessageData | kFlagMessageDataIsPartial));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(1));
    ExpectWriteByteArray(std::string(WireWriterImpl::kBlockSize, 'a'));
    EXPECT_CALL(mock_writable_parcel, GetDataSize)
        .WillOnce(Return(WireWriterImpl::kBlockSize));
    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 3)));

    EXPECT_CALL(mock_writable_parcel,
                WriteInt32(kFlagMessageData | kFlagSuffix));
    EXPECT_CALL(mock_writable_parcel, WriteInt32(2));
    ExpectWriteByteArray("a");
    EXPECT_CALL(mock_writable_parcel, GetDataSize).WillOnce(Return(1));
    EXPECT_CALL(mock_binder_ref,
                Transact(BinderTransportTxCode(kFirstCallId + 3)));

    // Use a new stream.
    auto tx =
        std::make_unique<Transaction>(kFirstCallId + 3, /*is_client=*/true);
    tx->SetPrefix({});
    tx->SetMethodRef("123");
    tx->SetData(std::string(2 * WireWriterImpl::kBlockSize + 1, 'a'));
    tx->SetSuffix({});
    EXPECT_TRUE(wire_writer.RpcCall(std::move(tx)).ok());
    grpc_core::ExecCtx::Get()->Flush();
  }
  grpc_core::ExecCtx::Get()->Flush();
}

}  // namespace grpc_binder

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
