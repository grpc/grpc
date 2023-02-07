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

#include "test/core/transport/binder/end2end/fake_binder.h"

#include <string>
#include <utility>

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"

namespace grpc_binder {
namespace end2end_testing {

TransactionProcessor* g_transaction_processor = nullptr;

int32_t FakeWritableParcel::GetDataSize() const { return data_size_; }

absl::Status FakeWritableParcel::WriteInt32(int32_t data) {
  data_.push_back(data);
  data_size_ += sizeof(int32_t);
  return absl::OkStatus();
}

absl::Status FakeWritableParcel::WriteInt64(int64_t data) {
  data_.push_back(data);
  data_size_ += sizeof(int64_t);
  return absl::OkStatus();
}

absl::Status FakeWritableParcel::WriteBinder(HasRawBinder* binder) {
  data_.push_back(binder->GetRawBinder());
  data_size_ += sizeof(void*);
  return absl::OkStatus();
}

absl::Status FakeWritableParcel::WriteString(absl::string_view s) {
  data_.push_back(std::string(s));
  data_size_ += s.size();
  return absl::OkStatus();
}

absl::Status FakeWritableParcel::WriteByteArray(const int8_t* buffer,
                                                int32_t length) {
  data_.push_back(std::vector<int8_t>(buffer, buffer + length));
  data_size_ += length;
  return absl::OkStatus();
}

int32_t FakeReadableParcel::GetDataSize() const { return data_size_; }

absl::Status FakeReadableParcel::ReadInt32(int32_t* data) {
  if (data_position_ >= data_.size() ||
      !absl::holds_alternative<int32_t>(data_[data_position_])) {
    return absl::InternalError("ReadInt32 failed");
  }
  *data = absl::get<int32_t>(data_[data_position_++]);
  return absl::OkStatus();
}

absl::Status FakeReadableParcel::ReadInt64(int64_t* data) {
  if (data_position_ >= data_.size() ||
      !absl::holds_alternative<int64_t>(data_[data_position_])) {
    return absl::InternalError("ReadInt64 failed");
  }
  *data = absl::get<int64_t>(data_[data_position_++]);
  return absl::OkStatus();
}

absl::Status FakeReadableParcel::ReadBinder(std::unique_ptr<Binder>* data) {
  if (data_position_ >= data_.size() ||
      !absl::holds_alternative<void*>(data_[data_position_])) {
    return absl::InternalError("ReadBinder failed");
  }
  void* endpoint = absl::get<void*>(data_[data_position_++]);
  if (!endpoint) return absl::InternalError("ReadBinder failed");
  *data = std::make_unique<FakeBinder>(static_cast<FakeEndpoint*>(endpoint));
  return absl::OkStatus();
}

absl::Status FakeReadableParcel::ReadString(std::string* str) {
  if (data_position_ >= data_.size() ||
      !absl::holds_alternative<std::string>(data_[data_position_])) {
    return absl::InternalError("ReadString failed");
  }
  *str = absl::get<std::string>(data_[data_position_++]);
  return absl::OkStatus();
}

absl::Status FakeReadableParcel::ReadByteArray(std::string* data) {
  if (data_position_ >= data_.size() ||
      !absl::holds_alternative<std::vector<int8_t>>(data_[data_position_])) {
    return absl::InternalError("ReadByteArray failed");
  }
  const std::vector<int8_t>& byte_array =
      absl::get<std::vector<int8_t>>(data_[data_position_++]);
  data->resize(byte_array.size());
  for (size_t i = 0; i < byte_array.size(); ++i) {
    (*data)[i] = byte_array[i];
  }
  return absl::OkStatus();
}

absl::Status FakeBinder::Transact(BinderTransportTxCode tx_code) {
  endpoint_->tunnel->EnQueueTransaction(endpoint_->other_end, tx_code,
                                        input_->MoveData());
  return absl::OkStatus();
}

FakeTransactionReceiver::FakeTransactionReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb transact_cb) {
  persistent_tx_receiver_ = &g_transaction_processor->NewPersistentTxReceiver(
      std::move(wire_reader_ref), std::move(transact_cb),
      std::make_unique<FakeBinderTunnel>());
}

std::unique_ptr<TransactionReceiver> FakeBinder::ConstructTxReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb cb) const {
  return std::make_unique<FakeTransactionReceiver>(wire_reader_ref, cb);
}

void* FakeTransactionReceiver::GetRawBinder() {
  return persistent_tx_receiver_->tunnel_->GetSendEndpoint();
}

std::unique_ptr<Binder> FakeTransactionReceiver::GetSender() const {
  return std::make_unique<FakeBinder>(
      persistent_tx_receiver_->tunnel_->GetSendEndpoint());
}

PersistentFakeTransactionReceiver::PersistentFakeTransactionReceiver(
    grpc_core::RefCountedPtr<WireReader> wire_reader_ref,
    TransactionReceiver::OnTransactCb cb,
    std::unique_ptr<FakeBinderTunnel> tunnel)
    : wire_reader_ref_(std::move(wire_reader_ref)),
      callback_(std::move(cb)),
      tunnel_(std::move(tunnel)) {
  FakeEndpoint* recv_endpoint = tunnel_->GetRecvEndpoint();
  recv_endpoint->owner = this;
}

TransactionProcessor::TransactionProcessor(absl::Duration delay)
    : delay_nsec_(absl::ToInt64Nanoseconds(delay)),
      tx_thread_(
          "process-thread",
          [](void* arg) {
            grpc_core::ExecCtx exec_ctx;
            auto* self = static_cast<TransactionProcessor*>(arg);
            self->ProcessLoop();
          },
          this),
      terminated_(false) {
  tx_thread_.Start();
}

void TransactionProcessor::SetDelay(absl::Duration delay) {
  delay_nsec_ = absl::ToInt64Nanoseconds(delay);
}

void TransactionProcessor::Terminate() {
  if (!terminated_.load(std::memory_order_seq_cst)) {
    gpr_log(GPR_INFO, "Terminating the processor");
    terminated_.store(true, std::memory_order_seq_cst);
    tx_thread_.Join();
    gpr_log(GPR_INFO, "Processor terminated");
  }
}

void TransactionProcessor::WaitForNextTransaction() {
  absl::Time now = absl::Now();
  if (now < deliver_time_) {
    absl::Duration diff = deliver_time_ - now;
    // Release the lock before going to sleep.
    mu_.Unlock();
    absl::SleepFor(diff);
    mu_.Lock();
  }
}

void TransactionProcessor::Flush() {
  while (true) {
    FakeEndpoint* target = nullptr;
    BinderTransportTxCode tx_code{};
    FakeData data;
    mu_.Lock();
    if (tx_queue_.empty()) {
      mu_.Unlock();
      break;
    }
    WaitForNextTransaction();
    std::tie(target, tx_code, data) = std::move(tx_queue_.front());
    tx_queue_.pop();
    if (!tx_queue_.empty()) {
      deliver_time_ = absl::Now() + GetRandomDelay();
    }
    mu_.Unlock();
    auto* tx_receiver =
        static_cast<PersistentFakeTransactionReceiver*>(target->owner);
    auto parcel = std::make_unique<FakeReadableParcel>(std::move(data));
    tx_receiver->Receive(tx_code, parcel.get()).IgnoreError();
  }
}

void TransactionProcessor::ProcessLoop() {
  while (!terminated_.load(std::memory_order_seq_cst)) {
    FakeEndpoint* target = nullptr;
    BinderTransportTxCode tx_code{};
    FakeData data;
    mu_.Lock();
    if (tx_queue_.empty()) {
      mu_.Unlock();
      continue;
    }
    WaitForNextTransaction();
    std::tie(target, tx_code, data) = std::move(tx_queue_.front());
    tx_queue_.pop();
    if (!tx_queue_.empty()) {
      deliver_time_ = absl::Now() + GetRandomDelay();
    }
    mu_.Unlock();
    auto* tx_receiver =
        static_cast<PersistentFakeTransactionReceiver*>(target->owner);
    auto parcel = std::make_unique<FakeReadableParcel>(std::move(data));
    tx_receiver->Receive(tx_code, parcel.get()).IgnoreError();
    grpc_core::ExecCtx::Get()->Flush();
  }
  Flush();
}

absl::Duration TransactionProcessor::GetRandomDelay() {
  int64_t delay =
      absl::Uniform<int64_t>(bit_gen_, delay_nsec_ / 2, delay_nsec_);
  return absl::Nanoseconds(delay);
}

void TransactionProcessor::EnQueueTransaction(FakeEndpoint* target,
                                              BinderTransportTxCode tx_code,
                                              FakeData data) {
  grpc_core::MutexLock lock(&mu_);
  if (tx_queue_.empty()) {
    // This is the first transaction in the queue. Compute its deliver time.
    deliver_time_ = absl::Now() + GetRandomDelay();
  }
  tx_queue_.emplace(target, tx_code, std::move(data));
}

FakeBinderTunnel::FakeBinderTunnel()
    : send_endpoint_(std::make_unique<FakeEndpoint>(this)),
      recv_endpoint_(std::make_unique<FakeEndpoint>(this)) {
  send_endpoint_->other_end = recv_endpoint_.get();
  recv_endpoint_->other_end = send_endpoint_.get();
}

std::pair<std::unique_ptr<Binder>, std::unique_ptr<TransactionReceiver>>
NewBinderPair(TransactionReceiver::OnTransactCb transact_cb) {
  auto tx_receiver = std::make_unique<FakeTransactionReceiver>(
      nullptr, std::move(transact_cb));
  std::unique_ptr<Binder> sender = tx_receiver->GetSender();
  return std::make_pair(std::move(sender), std::move(tx_receiver));
}

}  // namespace end2end_testing
}  // namespace grpc_binder
