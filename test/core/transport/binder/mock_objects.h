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

#ifndef GRPC_TEST_CORE_TRANSPORT_BINDER_MOCK_OBJECTS_H
#define GRPC_TEST_CORE_TRANSPORT_BINDER_MOCK_OBJECTS_H

#include <gmock/gmock.h>

#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/binder_constants.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

namespace grpc_binder {

class MockWritableParcel : public WritableParcel {
 public:
  MOCK_METHOD(int32_t, GetDataSize, (), (const, override));
  MOCK_METHOD(absl::Status, WriteInt32, (int32_t), (override));
  MOCK_METHOD(absl::Status, WriteInt64, (int64_t), (override));
  MOCK_METHOD(absl::Status, WriteBinder, (HasRawBinder*), (override));
  MOCK_METHOD(absl::Status, WriteString, (absl::string_view), (override));
  MOCK_METHOD(absl::Status, WriteByteArray, (const int8_t*, int32_t),
              (override));

  MockWritableParcel();
};

class MockReadableParcel : public ReadableParcel {
 public:
  MOCK_METHOD(int32_t, GetDataSize, (), (const, override));
  MOCK_METHOD(absl::Status, ReadInt32, (int32_t*), (override));
  MOCK_METHOD(absl::Status, ReadInt64, (int64_t*), (override));
  MOCK_METHOD(absl::Status, ReadBinder, (std::unique_ptr<Binder>*), (override));
  MOCK_METHOD(absl::Status, ReadByteArray, (std::string*), (override));
  MOCK_METHOD(absl::Status, ReadString, (std::string*), (override));

  MockReadableParcel();
};

class MockBinder : public Binder {
 public:
  MOCK_METHOD(void, Initialize, (), (override));
  MOCK_METHOD(absl::Status, PrepareTransaction, (), (override));
  MOCK_METHOD(absl::Status, Transact, (BinderTransportTxCode), (override));
  MOCK_METHOD(WritableParcel*, GetWritableParcel, (), (const, override));
  MOCK_METHOD(std::unique_ptr<TransactionReceiver>, ConstructTxReceiver,
              (grpc_core::RefCountedPtr<WireReader>,
               TransactionReceiver::OnTransactCb),
              (const, override));
  MOCK_METHOD(void*, GetRawBinder, (), (override));

  MockBinder();
  MockWritableParcel& GetWriter() { return mock_input_; }
  MockReadableParcel& GetReader() { return mock_output_; }

 private:
  MockWritableParcel mock_input_;
  MockReadableParcel mock_output_;
};

// TODO(waynetu): Implement transaction injection later for more thorough
// testing.
class MockTransactionReceiver : public TransactionReceiver {
 public:
  explicit MockTransactionReceiver(OnTransactCb transact_cb,
                                   BinderTransportTxCode code,
                                   MockReadableParcel* output) {
    if (code == BinderTransportTxCode::SETUP_TRANSPORT) {
      EXPECT_CALL(*output, ReadInt32).WillOnce([](int32_t* version) {
        *version = 1;
        return absl::OkStatus();
      });
    }
    transact_cb(static_cast<transaction_code_t>(code), output, /*uid=*/0)
        .IgnoreError();
  }

  MOCK_METHOD(void*, GetRawBinder, (), (override));
};

class MockWireWriter : public WireWriter {
 public:
  MOCK_METHOD(absl::Status, RpcCall, (std::unique_ptr<Transaction>),
              (override));
  MOCK_METHOD(absl::Status, SendAck, (int64_t), (override));
  MOCK_METHOD(void, OnAckReceived, (int64_t), (override));
};

class MockTransportStreamReceiver : public TransportStreamReceiver {
 public:
  MOCK_METHOD(void, RegisterRecvInitialMetadata,
              (StreamIdentifier, InitialMetadataCallbackType), (override));
  MOCK_METHOD(void, RegisterRecvMessage,
              (StreamIdentifier, MessageDataCallbackType), (override));
  MOCK_METHOD(void, RegisterRecvTrailingMetadata,
              (StreamIdentifier, TrailingMetadataCallbackType), (override));
  MOCK_METHOD(void, NotifyRecvInitialMetadata,
              (StreamIdentifier, absl::StatusOr<Metadata>), (override));
  MOCK_METHOD(void, NotifyRecvMessage,
              (StreamIdentifier, absl::StatusOr<std::string>), (override));
  MOCK_METHOD(void, NotifyRecvTrailingMetadata,
              (StreamIdentifier, absl::StatusOr<Metadata>, int), (override));
  MOCK_METHOD(void, CancelStream, (StreamIdentifier), (override));
};

}  // namespace grpc_binder

#endif  // GRPC_TEST_CORE_TRANSPORT_BINDER_MOCK_OBJECTS_H
