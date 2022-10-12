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
  return parcel_data_size_;
}

absl::Status ReadableParcelForFuzzing::ReadInt32(int32_t* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (values_.empty() || !values_.front().has_i32()) {
    return absl::InternalError("error");
  }
  *data = values_.front().i32();
  values_.pop();
  consumed_data_size_ += sizeof(int32_t);
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadInt64(int64_t* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (values_.empty() || !values_.front().has_i64()) {
    return absl::InternalError("error");
  }
  *data = values_.front().i64();
  values_.pop();
  consumed_data_size_ += sizeof(int64_t);
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadBinder(
    std::unique_ptr<Binder>* binder) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (values_.empty() || !values_.front().has_binder()) {
    return absl::InternalError("error");
  }
  *binder = std::make_unique<BinderForFuzzing>();
  values_.pop();
  consumed_data_size_ += sizeof(void*);
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadByteArray(std::string* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (values_.empty() || !values_.front().has_byte_array()) {
    return absl::InternalError("error");
  }
  *data = values_.front().byte_array();
  values_.pop();
  consumed_data_size_ += data->size();
  return absl::OkStatus();
}

absl::Status ReadableParcelForFuzzing::ReadString(std::string* data) {
  if (consumed_data_size_ >= kParcelDataSizeLimit) {
    return absl::InternalError("Parcel size limit exceeds");
  }
  if (values_.empty() || !values_.front().has_str()) {
    return absl::InternalError("error");
  }
  *data = values_.front().str();
  values_.pop();
  consumed_data_size_ += data->size();
  return absl::OkStatus();
}

void FuzzingLoop(
    binder_transport_fuzzer::IncomingParcels incoming_parcels,
    grpc_core::RefCountedPtr<grpc_binder::WireReader> wire_reader_ref,
    grpc_binder::TransactionReceiver::OnTransactCb callback) {
  {
    // Send SETUP_TRANSPORT request.
    std::unique_ptr<grpc_binder::ReadableParcel> parcel =
        std::make_unique<ReadableParcelForFuzzing>(
            incoming_parcels.setup_transport_transaction().parcel());
    callback(static_cast<transaction_code_t>(
                 grpc_binder::BinderTransportTxCode::SETUP_TRANSPORT),
             parcel.get(),
             /*uid=*/incoming_parcels.setup_transport_transaction().uid())
        .IgnoreError();
  }
  for (const auto& tx_iter : incoming_parcels.transactions()) {
    transaction_code_t tx_code = tx_iter.code();
    std::unique_ptr<grpc_binder::ReadableParcel> parcel =
        std::make_unique<ReadableParcelForFuzzing>(tx_iter.parcel());
    callback(tx_code, parcel.get(),
             /*uid=*/tx_iter.uid())
        .IgnoreError();
  }
  wire_reader_ref = nullptr;
}

TransactionReceiverForFuzzing::TransactionReceiverForFuzzing(
    binder_transport_fuzzer::IncomingParcels incoming_parcels,
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb cb) {
  gpr_log(GPR_INFO, "Construct TransactionReceiverForFuzzing");
  CreateFuzzingThread(FuzzingLoop, std::move(incoming_parcels),
                      std::move(wire_reader_ref), std::move(cb));
}

std::unique_ptr<TransactionReceiver> BinderForFuzzing::ConstructTxReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb cb) const {
  auto tx_receiver = std::make_unique<TransactionReceiverForFuzzing>(
      incoming_parcels_, wire_reader_ref, cb);
  return tx_receiver;
}

}  // namespace fuzzing
}  // namespace grpc_binder
