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

#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"

#include <grpc/support/log.h>

#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

#define RETURN_IF_ERROR(expr)           \
  do {                                  \
    const absl::Status status = (expr); \
    if (!status.ok()) return status;    \
  } while (0)

namespace grpc_binder {
namespace {

absl::StatusOr<Metadata> parse_metadata(const ReadableParcel* reader) {
  int num_header;
  RETURN_IF_ERROR(reader->ReadInt32(&num_header));
  gpr_log(GPR_INFO, "num_header = %d", num_header);
  std::vector<std::pair<std::string, std::string>> ret;
  for (int i = 0; i != num_header; i++) {
    int count;
    RETURN_IF_ERROR(reader->ReadInt32(&count));
    gpr_log(GPR_INFO, "count = %d", count);
    std::string key{};
    if (count > 0) RETURN_IF_ERROR(reader->ReadByteArray(&key));
    gpr_log(GPR_INFO, "key = %s", key.c_str());
    RETURN_IF_ERROR(reader->ReadInt32(&count));
    gpr_log(GPR_INFO, "count = %d", count);
    std::string value{};
    if (count > 0) RETURN_IF_ERROR(reader->ReadByteArray(&value));
    gpr_log(GPR_INFO, "value = %s", value.c_str());
    ret.push_back({key, value});
  }
  return ret;
}

}  // namespace

WireReaderImpl::WireReaderImpl(
    TransportStreamReceiver* transport_stream_receiver, bool is_client)
    : transport_stream_receiver_(transport_stream_receiver),
      is_client_(is_client) {}

WireReaderImpl::~WireReaderImpl() = default;

std::unique_ptr<WireWriter> WireReaderImpl::SetupTransport(
    std::unique_ptr<Binder> binder) {
  if (!is_client_) {
    gpr_log(GPR_ERROR, "Server-side SETUP_TRANSPORT is not implemented yet.");
    return nullptr;
  }

  gpr_log(GPR_INFO, "Setting up transport");
  binder->Initialize();
  gpr_log(GPR_INFO, "prepare transaction = %d",
          binder->PrepareTransaction().ok());

  // Only support client-side transport setup.
  SendSetupTransport(binder.get());
  RecvSetupTransport();
  return absl::make_unique<WireWriterImpl>(std::move(other_end_binder_));
}

void WireReaderImpl::SendSetupTransport(Binder* binder) {
  WritableParcel* writable_parcel = binder->GetWritableParcel();
  gpr_log(GPR_INFO, "data position = %d", writable_parcel->GetDataPosition());
  // gpr_log(GPR_INFO, "set data position to 0 = %d",
  // writer->SetDataPosition(0));
  gpr_log(GPR_INFO, "data position = %d", writable_parcel->GetDataPosition());
  int32_t version = 77;
  gpr_log(GPR_INFO, "write int32 = %d",
          writable_parcel->WriteInt32(version).ok());
  gpr_log(GPR_INFO, "data position = %d", writable_parcel->GetDataPosition());
  // The lifetime of the transaction receiver is the same as the wire writer's.
  // The transaction receiver is responsible for not calling the on-transact
  // callback when it's dead.
  tx_receiver_ = binder->ConstructTxReceiver(
      [this](transaction_code_t code, const ReadableParcel* readable_parcel) {
        return this->ProcessTransaction(code, readable_parcel);
      });

  gpr_log(GPR_INFO, "tx_receiver = %p", tx_receiver_->GetRawBinder());
  gpr_log(GPR_INFO, "AParcel_writeStrongBinder = %d",
          writable_parcel->WriteBinder(tx_receiver_.get()).ok());
  gpr_log(GPR_INFO, "AIBinder_transact = %d",
          binder->Transact(BinderTransportTxCode::SETUP_TRANSPORT).ok());
}

void WireReaderImpl::RecvSetupTransport() {
  // TODO(b/191941760): avoid blocking, handle wire_writer_noti lifetime
  // better
  gpr_log(GPR_INFO, "start waiting for noti");
  connection_noti_.WaitForNotification();
  gpr_log(GPR_INFO, "end waiting for noti");
}

absl::Status WireReaderImpl::ProcessTransaction(transaction_code_t code,
                                                const ReadableParcel* parcel) {
  gpr_log(GPR_INFO, __func__);
  gpr_log(GPR_INFO, "tx code = %u", code);
  if (code >= static_cast<unsigned>(kFirstCallId)) {
    gpr_log(GPR_INFO, "This is probably a Streaming Tx");
    return ProcessStreamingTransaction(code, parcel);
  }

  if (!(code >= static_cast<transaction_code_t>(
                    BinderTransportTxCode::SETUP_TRANSPORT) &&
        code <= static_cast<transaction_code_t>(
                    BinderTransportTxCode::PING_RESPONSE))) {
    gpr_log(GPR_ERROR,
            "Received unknown control message. Shutdown transport gracefully.");
    // TODO(waynetu): Shutdown transport gracefully.
    return absl::OkStatus();
  }

  switch (BinderTransportTxCode(code)) {
    case BinderTransportTxCode::SETUP_TRANSPORT: {
      // int datasize;
      int version;
      // getDataSize not supported until 31
      // gpr_log(GPR_INFO, "getDataSize = %d", AParcel_getDataSize(in,
      // &datasize));
      RETURN_IF_ERROR(parcel->ReadInt32(&version));
      // gpr_log(GPR_INFO, "data size = %d", datasize);
      gpr_log(GPR_INFO, "version = %d", version);
      std::unique_ptr<Binder> binder{};
      RETURN_IF_ERROR(parcel->ReadBinder(&binder));
      binder->Initialize();
      other_end_binder_ = std::move(binder);
      connection_noti_.Notify();
      break;
    }
    case BinderTransportTxCode::SHUTDOWN_TRANSPORT: {
      gpr_log(GPR_ERROR,
              "Received SHUTDOWN_TRANSPORT request but not implemented yet.");
      GPR_ASSERT(false);
      break;
    }
    case BinderTransportTxCode::ACKNOWLEDGE_BYTES: {
      int num_bytes = -1;
      RETURN_IF_ERROR(parcel->ReadInt32(&num_bytes));
      gpr_log(GPR_INFO, "received acknowledge bytes = %d", num_bytes);
      break;
    }
    case BinderTransportTxCode::PING: {
      GPR_ASSERT(!is_client_);
      int ping_id = -1;
      RETURN_IF_ERROR(parcel->ReadInt32(&ping_id));
      gpr_log(GPR_INFO, "received ping id = %d", ping_id);
      // TODO(waynetu): Ping back.
      break;
    }
    case BinderTransportTxCode::PING_RESPONSE: {
      int value = -1;
      RETURN_IF_ERROR(parcel->ReadInt32(&value));
      gpr_log(GPR_INFO, "received ping response = %d", value);
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status WireReaderImpl::ProcessStreamingTransaction(
    transaction_code_t code, const ReadableParcel* parcel) {
  int flags;
  RETURN_IF_ERROR(parcel->ReadInt32(&flags));
  gpr_log(GPR_INFO, "flags = %d", flags);

  // Ignore in-coming transaction with flag = 0 to match with Java
  // implementation.
  // TODO(waynetu): Check with grpc-java team to see whether this is the
  // intended behavior.
  // TODO(waynetu): What should be returned here?
  if (flags == 0) return absl::OkStatus();

  int status = flags >> 16;
  gpr_log(GPR_INFO, "status = %d", status);
  gpr_log(GPR_INFO, "FLAG_PREFIX = %d", (flags & kFlagPrefix));
  gpr_log(GPR_INFO, "FLAG_MESSAGE_DATA = %d", (flags & kFlagMessageData));
  gpr_log(GPR_INFO, "FLAG_SUFFIX = %d", (flags & kFlagSuffix));
  int seq_num;
  RETURN_IF_ERROR(parcel->ReadInt32(&seq_num));
  // TODO(waynetu): For now we'll just assume that the transactions commit in
  // the same order they're issued. The following assertion detects
  // out-of-order or missing transactions. WireReaderImpl should be fixed if
  // we indeed found such behavior.
  int32_t& expectation = expected_seq_num_[code];
  // TODO(mingcl): Don't assert here
  GPR_ASSERT(seq_num >= 0);
  GPR_ASSERT(seq_num == expectation && "Interleaved sequence number");
  // TODO(waynetu): According to the protocol, "The sequence number will wrap
  // around to 0 if more than 2^31 messages are sent." For now we'll just
  // assert that it never reach such circumstances.
  GPR_ASSERT(expectation < std::numeric_limits<int32_t>::max() &&
             "Sequence number too large");
  expectation++;
  gpr_log(GPR_INFO, "sequence number = %d", seq_num);
  if (flags & kFlagPrefix) {
    char method_ref[111];
    if (!is_client_) {
      RETURN_IF_ERROR(parcel->ReadString(method_ref));
    }
    absl::StatusOr<Metadata> initial_metadata_or_error = parse_metadata(parcel);
    if (!initial_metadata_or_error.ok()) {
      return initial_metadata_or_error.status();
    }
    if (!is_client_) {
      initial_metadata_or_error->emplace_back(":path", method_ref);
    }
    transport_stream_receiver_->NotifyRecvInitialMetadata(
        code, *initial_metadata_or_error);
  }
  if (flags & kFlagMessageData) {
    int count;
    RETURN_IF_ERROR(parcel->ReadInt32(&count));
    gpr_log(GPR_INFO, "count = %d", count);
    std::string msg_data{};
    if (count > 0) {
      RETURN_IF_ERROR(parcel->ReadByteArray(&msg_data));
    }
    gpr_log(GPR_INFO, "msg_data = %s", msg_data.c_str());
    transport_stream_receiver_->NotifyRecvMessage(code, msg_data);
  }
  if (flags & kFlagSuffix) {
    if (flags & kFlagStatusDescription) {
      // FLAG_STATUS_DESCRIPTION set
      char desc[111];
      RETURN_IF_ERROR(parcel->ReadString(desc));
      gpr_log(GPR_INFO, "description = %s", desc);
    }
    Metadata trailing_metadata;
    if (is_client_) {
      absl::StatusOr<Metadata> trailing_metadata_or_error =
          parse_metadata(parcel);
      if (!trailing_metadata_or_error.ok()) {
        return trailing_metadata_or_error.status();
      }
      trailing_metadata = *trailing_metadata_or_error;
    }
    transport_stream_receiver_->NotifyRecvTrailingMetadata(
        code, trailing_metadata, status);
  }
  return absl::OkStatus();
}

}  // namespace grpc_binder
