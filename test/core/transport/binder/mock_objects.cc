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

#include "test/core/transport/binder/mock_objects.h"

#include <memory>

#include "absl/memory/memory.h"

namespace grpc_binder {

using ::testing::Return;

MockReadableParcel::MockReadableParcel() {
  ON_CALL(*this, ReadBinder).WillByDefault([](std::unique_ptr<Binder>* binder) {
    *binder = std::make_unique<MockBinder>();
    return absl::OkStatus();
  });
  ON_CALL(*this, ReadInt32).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, ReadByteArray).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, ReadString).WillByDefault(Return(absl::OkStatus()));
}

MockWritableParcel::MockWritableParcel() {
  ON_CALL(*this, WriteInt32).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, WriteBinder).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, WriteString).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, WriteByteArray).WillByDefault(Return(absl::OkStatus()));
}

MockBinder::MockBinder() {
  ON_CALL(*this, PrepareTransaction).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, Transact).WillByDefault(Return(absl::OkStatus()));
  ON_CALL(*this, GetWritableParcel).WillByDefault(Return(&mock_input_));
  ON_CALL(*this, ConstructTxReceiver)
      .WillByDefault(
          [this](grpc_core::RefCountedPtr<WireReader> /*wire_reader_ref*/,
                 TransactionReceiver::OnTransactCb cb) {
            return std::make_unique<MockTransactionReceiver>(
                cb, BinderTransportTxCode::SETUP_TRANSPORT, &mock_output_);
          });
}

}  // namespace grpc_binder
