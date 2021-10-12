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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_BINDER_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_BINDER_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include "src/core/ext/transport/binder/wire_format/binder_constants.h"
#include "src/core/lib/gprpp/orphanable.h"

namespace grpc_binder {

class HasRawBinder {
 public:
  virtual ~HasRawBinder() = default;
  virtual void* GetRawBinder() = 0;
};

class Binder;

// TODO(waynetu): We might need other methods as well.
// TODO(waynetu): Find a better way to express the returned status than
// binder_status_t.
class WritableParcel {
 public:
  virtual ~WritableParcel() = default;
  virtual int32_t GetDataSize() const = 0;
  virtual absl::Status WriteInt32(int32_t data) = 0;
  virtual absl::Status WriteInt64(int64_t data) = 0;
  virtual absl::Status WriteBinder(HasRawBinder* binder) = 0;
  virtual absl::Status WriteString(absl::string_view s) = 0;
  virtual absl::Status WriteByteArray(const int8_t* buffer, int32_t length) = 0;

  absl::Status WriteByteArrayWithLength(absl::string_view buffer) {
    absl::Status status = WriteInt32(buffer.length());
    if (!status.ok()) return status;
    if (buffer.empty()) return absl::OkStatus();
    return WriteByteArray(reinterpret_cast<const int8_t*>(buffer.data()),
                          buffer.length());
  }
};

// TODO(waynetu): We might need other methods as well.
// TODO(waynetu): Find a better way to express the returned status than
// binder_status_t.
class ReadableParcel {
 public:
  virtual ~ReadableParcel() = default;
  virtual int32_t GetDataSize() const = 0;
  virtual absl::Status ReadInt32(int32_t* data) = 0;
  virtual absl::Status ReadInt64(int64_t* data) = 0;
  virtual absl::Status ReadBinder(std::unique_ptr<Binder>* data) = 0;
  virtual absl::Status ReadByteArray(std::string* data) = 0;
  virtual absl::Status ReadString(std::string* str) = 0;
};

class TransactionReceiver : public HasRawBinder {
 public:
  using OnTransactCb =
      std::function<absl::Status(transaction_code_t, ReadableParcel*, int uid)>;

  ~TransactionReceiver() override = default;
};

class WireReader;

class Binder : public HasRawBinder {
 public:
  ~Binder() override = default;

  virtual void Initialize() = 0;
  virtual absl::Status PrepareTransaction() = 0;
  virtual absl::Status Transact(BinderTransportTxCode tx_code) = 0;

  virtual WritableParcel* GetWritableParcel() const = 0;

  // TODO(waynetu): Can we decouple the receiver from the binder?
  virtual std::unique_ptr<TransactionReceiver> ConstructTxReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb transact_cb) const = 0;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_BINDER_H
