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

#include "src/core/ext/transport/binder/wire_format/wire_reader_impl.h"

#ifndef GRPC_NO_BINDER

#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"

#include <grpc/support/log.h>

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

const int32_t kWireFormatVersion = 1;

absl::StatusOr<Metadata> parse_metadata(ReadableParcel* reader) {
  int num_header;
  RETURN_IF_ERROR(reader->ReadInt32(&num_header));
  gpr_log(GPR_INFO, "num_header = %d", num_header);
  if (num_header < 0) {
    return absl::InvalidArgumentError("num_header cannot be negative");
  }
  std::vector<std::pair<std::string, std::string>> ret;
  for (int i = 0; i < num_header; i++) {
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
    ret.emplace_back(key, value);
  }
  return ret;
}

}  // namespace

WireReaderImpl::WireReaderImpl(
    std::shared_ptr<TransportStreamReceiver> transport_stream_receiver,
    bool is_client,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy> security_policy,
    std::function<void()> on_destruct_callback)
    : transport_stream_receiver_(std::move(transport_stream_receiver)),
      is_client_(is_client),
      security_policy_(security_policy),
      on_destruct_callback_(on_destruct_callback) {}

WireReaderImpl::~WireReaderImpl() {
  if (on_destruct_callback_) {
    on_destruct_callback_();
  }
}

std::shared_ptr<WireWriter> WireReaderImpl::SetupTransport(
    std::unique_ptr<Binder> binder) {
  gpr_log(GPR_INFO, "Setting up transport");
  if (!is_client_) {
    SendSetupTransport(binder.get());
    {
      grpc_core::MutexLock lock(&mu_);
      connected_ = true;
      wire_writer_ = std::make_shared<WireWriterImpl>(std::move(binder));
    }
    return wire_writer_;
  } else {
    SendSetupTransport(binder.get());
    auto other_end_binder = RecvSetupTransport();
    {
      grpc_core::MutexLock lock(&mu_);
      connected_ = true;
      wire_writer_ =
          std::make_shared<WireWriterImpl>(std::move(other_end_binder));
    }
    return wire_writer_;
  }
}

void WireReaderImpl::SendSetupTransport(Binder* binder) {
  binder->Initialize();
  gpr_log(GPR_INFO, "prepare transaction = %d",
          binder->PrepareTransaction().ok());
  WritableParcel* writable_parcel = binder->GetWritableParcel();
  gpr_log(GPR_INFO, "write int32 = %d",
          writable_parcel->WriteInt32(kWireFormatVersion).ok());
  // The lifetime of the transaction receiver is the same as the wire writer's.
  // The transaction receiver is responsible for not calling the on-transact
  // callback when it's dead.
  // Give TransactionReceiver a Ref() since WireReader cannot be destructed
  // during callback execution. TransactionReceiver should make sure that the
  // callback owns a Ref() when it's being invoked.
  tx_receiver_ = binder->ConstructTxReceiver(
      /*wire_reader_ref=*/Ref(),
      [this](transaction_code_t code, ReadableParcel* readable_parcel,
             int uid) {
        return this->ProcessTransaction(code, readable_parcel, uid);
      });

  gpr_log(GPR_INFO, "tx_receiver = %p", tx_receiver_->GetRawBinder());
  gpr_log(GPR_INFO, "AParcel_writeStrongBinder = %d",
          writable_parcel->WriteBinder(tx_receiver_.get()).ok());
  gpr_log(GPR_INFO, "AIBinder_transact = %d",
          binder->Transact(BinderTransportTxCode::SETUP_TRANSPORT).ok());
}

std::unique_ptr<Binder> WireReaderImpl::RecvSetupTransport() {
  // TODO(b/191941760): avoid blocking, handle wire_writer_noti lifetime
  // better
  gpr_log(GPR_INFO, "start waiting for noti");
  connection_noti_.WaitForNotification();
  gpr_log(GPR_INFO, "end waiting for noti");
  return std::move(other_end_binder_);
}

absl::Status WireReaderImpl::ProcessTransaction(transaction_code_t code,
                                                ReadableParcel* parcel,
                                                int uid) {
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
    gpr_log(GPR_INFO,
            "Received unknown control message. Shutdown transport gracefully.");
    // TODO(waynetu): Shutdown transport gracefully.
    return absl::OkStatus();
  }

  grpc_core::MutexLock lock(&mu_);

  if (BinderTransportTxCode(code) != BinderTransportTxCode::SETUP_TRANSPORT &&
      !connected_) {
    return absl::InvalidArgumentError("Transports not connected yet");
  }

  // TODO(mingcl): See if we want to check the security policy for every RPC
  // call or just during transport setup.

  switch (BinderTransportTxCode(code)) {
    case BinderTransportTxCode::SETUP_TRANSPORT: {
      if (recvd_setup_transport_) {
        return absl::InvalidArgumentError(
            "Already received a SETUP_TRANSPORT request");
      }
      recvd_setup_transport_ = true;

      gpr_log(GPR_ERROR, "calling uid = %d", uid);
      if (!security_policy_->IsAuthorized(uid)) {
        return absl::PermissionDeniedError(
            "UID " + std::to_string(uid) +
            " is not allowed to connect to this "
            "transport according to security policy.");
      }

      int version;
      RETURN_IF_ERROR(parcel->ReadInt32(&version));
      gpr_log(GPR_INFO, "The other end respond with version = %d", version);
      // We only support this single lowest possible version, so server must
      // respond that version too.
      if (version != kWireFormatVersion) {
        gpr_log(GPR_ERROR,
                "The other end respond with version = %d, but we requested "
                "version %d, trying to continue anyway",
                version, kWireFormatVersion);
      }
      std::unique_ptr<Binder> binder{};
      RETURN_IF_ERROR(parcel->ReadBinder(&binder));
      if (!binder) {
        return absl::InternalError("Read NULL binder from the parcel");
      }
      binder->Initialize();
      other_end_binder_ = std::move(binder);
      connection_noti_.Notify();
      break;
    }
    case BinderTransportTxCode::SHUTDOWN_TRANSPORT: {
      gpr_log(GPR_ERROR,
              "Received SHUTDOWN_TRANSPORT request but not implemented yet.");
      return absl::UnimplementedError("SHUTDOWN_TRANSPORT");
    }
    case BinderTransportTxCode::ACKNOWLEDGE_BYTES: {
      int64_t num_bytes = -1;
      RETURN_IF_ERROR(parcel->ReadInt64(&num_bytes));
      gpr_log(GPR_INFO, "received acknowledge bytes = %lld",
              static_cast<long long>(num_bytes));
      wire_writer_->OnAckReceived(num_bytes);
      break;
    }
    case BinderTransportTxCode::PING: {
      if (is_client_) {
        return absl::FailedPreconditionError("Receive PING request in client");
      }
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
    transaction_code_t code, ReadableParcel* parcel) {
  grpc_core::MutexLock lock(&mu_);
  if (!connected_) {
    return absl::InvalidArgumentError("Transports not connected yet");
  }

  // Indicate which callbacks should be cancelled. It will be initialized as the
  // flags the in-coming transaction carries, and when a particular callback is
  // completed, the corresponding bit in cancellation_flag will be set to 0 so
  // that we won't cancel it afterward.
  int cancellation_flags = 0;
  absl::Status status =
      ProcessStreamingTransactionImpl(code, parcel, &cancellation_flags);
  if (!status.ok()) {
    gpr_log(GPR_ERROR, "Failed to process streaming transaction: %s",
            status.ToString().c_str());
    // Something went wrong when receiving transaction. Cancel failed requests.
    if (cancellation_flags & kFlagPrefix) {
      gpr_log(GPR_INFO, "cancelling initial metadata");
      transport_stream_receiver_->NotifyRecvInitialMetadata(code, status);
    }
    if (cancellation_flags & kFlagMessageData) {
      gpr_log(GPR_INFO, "cancelling message data");
      transport_stream_receiver_->NotifyRecvMessage(code, status);
    }
    if (cancellation_flags & kFlagSuffix) {
      gpr_log(GPR_INFO, "cancelling trailing metadata");
      transport_stream_receiver_->NotifyRecvTrailingMetadata(code, status, 0);
    }
  }
  if ((num_incoming_bytes_ - num_acknowledged_bytes_) >= kFlowControlAckBytes) {
    GPR_ASSERT(wire_writer_);
    absl::Status ack_status = wire_writer_->SendAck(num_incoming_bytes_);
    if (status.ok()) {
      status = ack_status;
    }
    num_acknowledged_bytes_ = num_incoming_bytes_;
  }
  return status;
}

absl::Status WireReaderImpl::ProcessStreamingTransactionImpl(
    transaction_code_t code, ReadableParcel* parcel, int* cancellation_flags) {
  GPR_ASSERT(cancellation_flags);
  num_incoming_bytes_ += parcel->GetDataSize();

  int flags;
  RETURN_IF_ERROR(parcel->ReadInt32(&flags));
  gpr_log(GPR_INFO, "flags = %d", flags);
  *cancellation_flags = flags;

  // Ignore in-coming transaction with flag = 0 to match with Java
  // implementation.
  // TODO(waynetu): Check with grpc-java team to see whether this is the
  // intended behavior.
  // TODO(waynetu): What should be returned here?
  if (flags == 0) {
    gpr_log(GPR_INFO, "[WARNING] Receive empty transaction. Ignored.");
    return absl::OkStatus();
  }

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
  if (seq_num < 0 || seq_num != expectation) {
    // Unexpected sequence number.
    return absl::InternalError("Unexpected sequence number");
  }
  // TODO(waynetu): According to the protocol, "The sequence number will wrap
  // around to 0 if more than 2^31 messages are sent." For now we'll just
  // assert that it never reach such circumstances.
  GPR_ASSERT(expectation < std::numeric_limits<int32_t>::max() &&
             "Sequence number too large");
  expectation++;
  gpr_log(GPR_INFO, "sequence number = %d", seq_num);
  if (flags & kFlagPrefix) {
    std::string method_ref;
    if (!is_client_) {
      RETURN_IF_ERROR(parcel->ReadString(&method_ref));
    }
    absl::StatusOr<Metadata> initial_metadata_or_error = parse_metadata(parcel);
    if (!initial_metadata_or_error.ok()) {
      return initial_metadata_or_error.status();
    }
    if (!is_client_) {
      initial_metadata_or_error->emplace_back(":path",
                                              std::string("/") + method_ref);
    }
    transport_stream_receiver_->NotifyRecvInitialMetadata(
        code, *initial_metadata_or_error);
    *cancellation_flags &= ~kFlagPrefix;
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
    message_buffer_[code] += msg_data;
    if ((flags & kFlagMessageDataIsPartial) == 0) {
      std::string s = std::move(message_buffer_[code]);
      message_buffer_.erase(code);
      transport_stream_receiver_->NotifyRecvMessage(code, std::move(s));
    }
    *cancellation_flags &= ~kFlagMessageData;
  }
  if (flags & kFlagSuffix) {
    if (flags & kFlagStatusDescription) {
      // FLAG_STATUS_DESCRIPTION set
      std::string desc;
      RETURN_IF_ERROR(parcel->ReadString(&desc));
      gpr_log(GPR_INFO, "description = %s", desc.c_str());
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
        code, std::move(trailing_metadata), status);
    *cancellation_flags &= ~kFlagSuffix;
  }
  return absl::OkStatus();
}

}  // namespace grpc_binder
#endif
