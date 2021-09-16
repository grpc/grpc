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

#ifndef GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_FUZZERS_FUZZER_UTILS_H
#define GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_FUZZERS_FUZZER_UTILS_H

#include <fuzzer/FuzzedDataProvider.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"

namespace grpc_binder {
namespace fuzzing {

// A WritableParcel implementation that simply does nothing. Don't use
// MockWritableParcel here since capturing calls is expensive.
class NoOpWritableParcel : public WritableParcel {
 public:
  int32_t GetDataSize() const override { return 0; }
  absl::Status WriteInt32(int32_t /*data*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteInt64(int64_t /*data*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteBinder(HasRawBinder* /*binder*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteString(absl::string_view /*s*/) override {
    return absl::OkStatus();
  }
  absl::Status WriteByteArray(const int8_t* /*buffer*/,
                              int32_t /*length*/) override {
    return absl::OkStatus();
  }
};

// Binder implementation used in fuzzing.
//
// Most of its the functionalities are no-op, except ConstructTxReceiver now
// returns a TranasctionReceiverForFuzzing.
class BinderForFuzzing : public Binder {
 public:
  BinderForFuzzing() : input_(absl::make_unique<NoOpWritableParcel>()) {}

  BinderForFuzzing(const uint8_t* data, size_t size)
      : data_(data),
        size_(size),
        input_(absl::make_unique<NoOpWritableParcel>()) {}

  void Initialize() override {}
  absl::Status PrepareTransaction() override { return absl::OkStatus(); }

  absl::Status Transact(BinderTransportTxCode /*tx_code*/) override {
    return absl::OkStatus();
  }

  std::unique_ptr<TransactionReceiver> ConstructTxReceiver(
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb cb) const override;

  WritableParcel* GetWritableParcel() const override { return input_.get(); }
  void* GetRawBinder() override { return nullptr; }

 private:
  const uint8_t* data_;
  size_t size_;
  std::unique_ptr<WritableParcel> input_;
};

// ReadableParcel implementation used in fuzzing.
//
// It consumes a FuzzedDataProvider, and returns fuzzed data upon user's
// requests. Each operation can also fail per fuzzer's request by checking the
// next bool in the data stream.
class ReadableParcelForFuzzing : public ReadableParcel {
 public:
  ReadableParcelForFuzzing(FuzzedDataProvider* data_provider,
                           bool is_setup_transport)
      : data_provider_(data_provider),
        is_setup_transport_(is_setup_transport),
        consumed_data_size_(0) {}

  int32_t GetDataSize() const override;
  absl::Status ReadInt32(int32_t* data) override;
  absl::Status ReadInt64(int64_t* data) override;
  absl::Status ReadBinder(std::unique_ptr<Binder>* binder) override;
  absl::Status ReadByteArray(std::string* data) override;
  absl::Status ReadString(std::string* data) override;

 private:
  FuzzedDataProvider* data_provider_;
  // Whether this parcel contains a SETUP_TRANSPORT request. If it is, we will
  // avoid returning errors in the Read* functions so that the fuzzer will not
  // be blocked waiting for the correct request.
  bool is_setup_transport_;

  static constexpr size_t kParcelDataSizeLimit = 1024 * 1024;
  size_t consumed_data_size_;
};

void JoinFuzzingThread();

void FuzzingLoop(const uint8_t* data, size_t size,
                 grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
                 TransactionReceiver::OnTransactCb callback);

// TransactionReceiver implementation used in fuzzing.
//
// When constructed, start sending fuzzed requests to the client. When all the
// bytes are consumed, the reference to WireReader will be released.
class TranasctionReceiverForFuzzing : public TransactionReceiver {
 public:
  TranasctionReceiverForFuzzing(
      const uint8_t* data, size_t size,
      grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
      TransactionReceiver::OnTransactCb cb);

  void* GetRawBinder() override { return nullptr; }
};

}  // namespace fuzzing
}  // namespace grpc_binder

#endif  // GRPC_TEST_CORE_TRANSPORT_BINDER_END2END_FUZZERS_FUZZER_UTILS_H
