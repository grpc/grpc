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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

#ifndef GRPC_NO_BINDER

#include <utility>

#include <grpc/support/log.h>

#define RETURN_IF_ERROR(expr)           \
  do {                                  \
    const absl::Status status = (expr); \
    if (!status.ok()) return status;    \
  } while (0)

namespace grpc_binder {
WireWriterImpl::WireWriterImpl(std::unique_ptr<Binder> binder)
    : binder_(std::move(binder)) {}

absl::Status WireWriterImpl::WriteInitialMetadata(const Transaction& tx,
                                                  WritableParcel* parcel) {
  if (tx.IsClient()) {
    // Only client sends method ref.
    RETURN_IF_ERROR(parcel->WriteString(tx.GetMethodRef()));
  }
  RETURN_IF_ERROR(parcel->WriteInt32(tx.GetPrefixMetadata().size()));
  for (const auto& md : tx.GetPrefixMetadata()) {
    RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(md.first));
    RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(md.second));
  }
  return absl::OkStatus();
}

absl::Status WireWriterImpl::WriteTrailingMetadata(const Transaction& tx,
                                                   WritableParcel* parcel) {
  if (tx.IsServer()) {
    if (tx.GetFlags() & kFlagStatusDescription) {
      RETURN_IF_ERROR(parcel->WriteString(tx.GetStatusDesc()));
    }
    RETURN_IF_ERROR(parcel->WriteInt32(tx.GetSuffixMetadata().size()));
    for (const auto& md : tx.GetSuffixMetadata()) {
      RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(md.first));
      RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(md.second));
    }
  } else {
    // client suffix currently is always empty according to the wireformat
    if (!tx.GetSuffixMetadata().empty()) {
      gpr_log(GPR_ERROR, "Got non-empty suffix metadata from client.");
    }
  }
  return absl::OkStatus();
}

const int64_t WireWriterImpl::kBlockSize = 16 * 1024;
const int64_t WireWriterImpl::kFlowControlWindowSize = 128 * 1024;

bool WireWriterImpl::CanBeSentInOneTransaction(const Transaction& tx) const {
  return (tx.GetFlags() & kFlagMessageData) == 0 ||
         tx.GetMessageData().size() <= kBlockSize;
}

absl::Status WireWriterImpl::RpcCallFastPath(const Transaction& tx) {
  int& seq = seq_num_[tx.GetTxCode()];
  // Fast path: send data in one transaction.
  RETURN_IF_ERROR(binder_->PrepareTransaction());
  WritableParcel* parcel = binder_->GetWritableParcel();
  RETURN_IF_ERROR(parcel->WriteInt32(tx.GetFlags()));
  RETURN_IF_ERROR(parcel->WriteInt32(seq++));
  if (tx.GetFlags() & kFlagPrefix) {
    RETURN_IF_ERROR(WriteInitialMetadata(tx, parcel));
  }
  if (tx.GetFlags() & kFlagMessageData) {
    RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(tx.GetMessageData()));
  }
  if (tx.GetFlags() & kFlagSuffix) {
    RETURN_IF_ERROR(WriteTrailingMetadata(tx, parcel));
  }
  // FIXME(waynetu): Construct BinderTransportTxCode from an arbitrary integer
  // is an undefined behavior.
  return binder_->Transact(BinderTransportTxCode(tx.GetTxCode()));
}

bool WireWriterImpl::WaitForAcknowledgement() {
  if (num_outgoing_bytes_ < num_acknowledged_bytes_ + kFlowControlWindowSize) {
    return true;
  }
  absl::Time deadline = absl::Now() + absl::Seconds(1);
  do {
    if (cv_.WaitWithDeadline(&mu_, deadline)) {
      return false;
    }
    if (absl::Now() >= deadline) {
      return false;
    }
  } while (num_outgoing_bytes_ >=
           num_acknowledged_bytes_ + kFlowControlWindowSize);
  return true;
}

absl::Status WireWriterImpl::RpcCall(const Transaction& tx) {
  // TODO(mingcl): check tx_code <= last call id
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(tx.GetTxCode() >= kFirstCallId);
  if (CanBeSentInOneTransaction(tx)) {
    return RpcCallFastPath(tx);
  }
  // Slow path: the message data is too large to fit in one transaction.
  int& seq = seq_num_[tx.GetTxCode()];
  int original_flags = tx.GetFlags();
  GPR_ASSERT(original_flags & kFlagMessageData);
  absl::string_view data = tx.GetMessageData();
  size_t bytes_sent = 0;
  while (bytes_sent < data.size()) {
    if (!WaitForAcknowledgement()) {
      return absl::InternalError("Timeout waiting for acknowledgement");
    }
    RETURN_IF_ERROR(binder_->PrepareTransaction());
    WritableParcel* parcel = binder_->GetWritableParcel();
    size_t size =
        std::min(static_cast<size_t>(kBlockSize), data.size() - bytes_sent);
    int flags = kFlagMessageData;
    if (bytes_sent == 0) {
      // This is the first transaction. Include initial metadata if there's any.
      if (original_flags & kFlagPrefix) {
        flags |= kFlagPrefix;
      }
    }
    if (bytes_sent + kBlockSize >= data.size()) {
      // This is the last transaction. Include trailing metadata if there's any.
      if (original_flags & kFlagSuffix) {
        flags |= kFlagSuffix;
      }
    } else {
      // There are more messages to send.
      flags |= kFlagMessageDataIsPartial;
    }
    RETURN_IF_ERROR(parcel->WriteInt32(flags));
    RETURN_IF_ERROR(parcel->WriteInt32(seq++));
    if (flags & kFlagPrefix) {
      RETURN_IF_ERROR(WriteInitialMetadata(tx, parcel));
    }
    RETURN_IF_ERROR(
        parcel->WriteByteArrayWithLength(data.substr(bytes_sent, size)));
    if (flags & kFlagSuffix) {
      RETURN_IF_ERROR(WriteTrailingMetadata(tx, parcel));
    }
    num_outgoing_bytes_ += parcel->GetDataSize();
    RETURN_IF_ERROR(binder_->Transact(BinderTransportTxCode(tx.GetTxCode())));
    bytes_sent += size;
  }
  return absl::OkStatus();
}

absl::Status WireWriterImpl::SendAck(int64_t num_bytes) {
  grpc_core::MutexLock lock(&mu_);
  RETURN_IF_ERROR(binder_->PrepareTransaction());
  WritableParcel* parcel = binder_->GetWritableParcel();
  RETURN_IF_ERROR(parcel->WriteInt64(num_bytes));
  return binder_->Transact(BinderTransportTxCode::ACKNOWLEDGE_BYTES);
}

void WireWriterImpl::OnAckReceived(int64_t num_bytes) {
  grpc_core::MutexLock lock(&mu_);
  num_acknowledged_bytes_ = std::max(num_acknowledged_bytes_, num_bytes);
  cv_.Signal();
}

}  // namespace grpc_binder
#endif
