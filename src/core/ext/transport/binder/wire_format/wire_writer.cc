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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

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

absl::Status WireWriterImpl::RpcCall(const Transaction& tx) {
  // TODO(mingcl): check tx_code <= last call id
  grpc_core::MutexLock lock(&mu_);
  GPR_ASSERT(tx.GetTxCode() >= kFirstCallId);
  RETURN_IF_ERROR(binder_->PrepareTransaction());
  WritableParcel* parcel = binder_->GetWritableParcel();
  {
    //  fill parcel
    RETURN_IF_ERROR(parcel->WriteInt32(tx.GetFlags()));
    RETURN_IF_ERROR(parcel->WriteInt32(tx.GetSeqNum()));
    if (tx.GetFlags() & kFlagPrefix) {
      // prefix set
      if (tx.IsClient()) {
        // Only client sends method ref.
        RETURN_IF_ERROR(parcel->WriteString(tx.GetMethodRef()));
      }
      RETURN_IF_ERROR(parcel->WriteInt32(tx.GetPrefixMetadata().size()));
      for (const auto& md : tx.GetPrefixMetadata()) {
        RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(md.first));
        RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(md.second));
      }
    }
    if (tx.GetFlags() & kFlagMessageData) {
      RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(tx.GetMessageData()));
    }
    if (tx.GetFlags() & kFlagSuffix) {
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
    }
  }
  // FIXME(waynetu): Construct BinderTransportTxCode from an arbitrary integer
  // is an undefined behavior.
  return binder_->Transact(BinderTransportTxCode(tx.GetTxCode()));
}

absl::Status WireWriterImpl::Ack(int64_t num_bytes) {
  grpc_core::MutexLock lock(&mu_);
  RETURN_IF_ERROR(binder_->PrepareTransaction());
  WritableParcel* parcel = binder_->GetWritableParcel();
  RETURN_IF_ERROR(parcel->WriteInt64(num_bytes));
  return binder_->Transact(BinderTransportTxCode::ACKNOWLEDGE_BYTES);
}

}  // namespace grpc_binder
