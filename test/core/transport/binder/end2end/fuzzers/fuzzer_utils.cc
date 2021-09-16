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

#include "test/core/transport/binder/end2end/fuzzers/fuzzer_utils.h"

namespace grpc_binder {
namespace fuzzing {

namespace {

std::thread* g_fuzzing_thread = nullptr;

template <typename... Args>
void CreateFuzzingThread(Args&&... args) {
  GPR_ASSERT(g_fuzzing_thread == nullptr);
  g_fuzzing_thread = new std::thread(std::forward<Args>(args)...);
}

}  // namespace

void JoinFuzzingThread() {
  if (g_fuzzing_thread) {
    g_fuzzing_thread->join();
    delete g_fuzzing_thread;
    g_fuzzing_thread = nullptr;
  }
}

int32_t ReadableParcelForFuzzing::GetDataSize() const {
  return data_provider_->ConsumeIntegral<int32_t>();
}

absl::Status ReadableParcelForFuzzing::ReadInt32(int32_t* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (!is_setup_transport_ && data_provider_->ConsumeBool()) {
    return absl::InternalError("error");
  }
  *data = data_provider_->ConsumeIntegral<int32_t>();
  consumed_data_size_ += sizeof(int32_t);
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadInt64(int64_t* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (!is_setup_transport_ && data_provider_->ConsumeBool()) {
    return absl::InternalError("error");
  }
  *data = data_provider_->ConsumeIntegral<int64_t>();
  consumed_data_size_ += sizeof(int64_t);
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadBinder(
    std::unique_ptr<Binder>* binder) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (!is_setup_transport_ && data_provider_->ConsumeBool()) {
    return absl::InternalError("error");
  }
  *binder = absl::make_unique<BinderForFuzzing>();
  consumed_data_size_ += sizeof(void*);
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadByteArray(std::string* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (!is_setup_transport_ && data_provider_->ConsumeBool()) {
    return absl::InternalError("error");
  }
  *data = data_provider_->ConsumeRandomLengthString(100);
  consumed_data_size_ += data->size();
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadString(std::string* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (!is_setup_transport_ && data_provider_->ConsumeBool()) {
    return absl::InternalError("error");
  }
  *data = data_provider_->ConsumeRandomLengthString(100);
  consumed_data_size_ += data->size();
  return absl::OkStatus();
}

void FuzzingLoop(
    const uint8_t* data, size_t size,
    grpc_core::RefCountedPtr<grpc_binder::WireReader> wire_reader_ref,
    grpc_binder::TransactionReceiver::OnTransactCb callback) {
  FuzzedDataProvider data_provider(data, size);
  {
    // Send SETUP_TRANSPORT request.
    std::unique_ptr<grpc_binder::ReadableParcel> parcel =
        absl::make_unique<ReadableParcelForFuzzing>(
            &data_provider,
            /*is_setup_transport=*/true);
    callback(static_cast<transaction_code_t>(
                 grpc_binder::BinderTransportTxCode::SETUP_TRANSPORT),
             parcel.get(), /*uid=*/data_provider.ConsumeIntegral<int>())
        .IgnoreError();
  }
  while (data_provider.remaining_bytes() > 0) {
    transaction_code_t tx_code =
        data_provider.ConsumeIntegralInRange<transaction_code_t>(
            0, LAST_CALL_TRANSACTION);
    std::unique_ptr<grpc_binder::ReadableParcel> parcel =
        absl::make_unique<ReadableParcelForFuzzing>(
            &data_provider,
            /*is_setup_transport=*/false);
    callback(tx_code, parcel.get(),
             /*uid=*/data_provider.ConsumeIntegral<int>())
        .IgnoreError();
  }
  wire_reader_ref = nullptr;
}

TranasctionReceiverForFuzzing::TranasctionReceiverForFuzzing(
    const uint8_t* data, size_t size,
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb cb) {
  gpr_log(GPR_INFO, "Construct TranasctionReceiverForFuzzing");
  CreateFuzzingThread(FuzzingLoop, data, size, std::move(wire_reader_ref),
                      std::move(cb));
}

std::unique_ptr<TransactionReceiver> BinderForFuzzing::ConstructTxReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb cb) const {
  return absl::make_unique<TranasctionReceiverForFuzzing>(data_, size_,
                                                          wire_reader_ref, cb);
}

}  // namespace fuzzing
}  // namespace grpc_binder
