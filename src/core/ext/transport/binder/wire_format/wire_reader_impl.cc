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

#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"

#include <grpc/support/log.h>

#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/status_helper.h"

namespace grpc_binder {
namespace {

const int32_t kWireFormatVersion = 1;
const char kAuthorityMetadataKey[] = ":authority";

absl::StatusOr<Metadata> parse_metadata(ReadableParcel* reader) {
  int num_header;
  GRPC_RETURN_IF_ERROR(reader->ReadInt32(&num_header));
  if (num_header < 0) {
    return absl::InvalidArgumentError("num_header cannot be negative");
  }
  std::vector<std::pair<std::string, std::string>> ret;
  for (int i = 0; i < num_header; i++) {
    int count;
    GRPC_RETURN_IF_ERROR(reader->ReadInt32(&count));
    std::string key{};
    if (count > 0) GRPC_RETURN_IF_ERROR(reader->ReadByteArray(&key));
    GRPC_RETURN_IF_ERROR(reader->ReadInt32(&count));
    std::string value{};
    if (count > 0) GRPC_RETURN_IF_ERROR(reader->ReadByteArray(&value));
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
  if (!is_client_) {
    connected_ = true;
    SendSetupTransport(binder.get());
    {
      grpc_core::MutexLock lock(&mu_);
      wire_writer_ = std::make_shared<WireWriterImpl>(std::move(binder));
    }
    wire_writer_ready_notification_.Notify();
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
    wire_writer_ready_notification_.Notify();
    return wire_writer_;
  }
}

void WireReaderImpl::SendSetupTransport(Binder* binder) {
  binder->Initialize();
  gpr_log(GPR_DEBUG, "prepare transaction = %d",
          binder->PrepareTransaction().ok());
  WritableParcel* writable_parcel = binder->GetWritableParcel();
  gpr_log(GPR_DEBUG, "write int32 = %d",
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

  gpr_log(GPR_DEBUG, "tx_receiver = %p", tx_receiver_->GetRawBinder());
  gpr_log(GPR_DEBUG, "AParcel_writeStrongBinder = %d",
          writable_parcel->WriteBinder(tx_receiver_.get()).ok());
  gpr_log(GPR_DEBUG, "AIBinder_transact = %d",
          binder->Transact(BinderTransportTxCode::SETUP_TRANSPORT).ok());
}

std::unique_ptr<Binder> WireReaderImpl::RecvSetupTransport() {
  // TODO(b/191941760): avoid blocking, handle wire_writer_noti lifetime
  // better
  gpr_log(GPR_DEBUG, "start waiting for noti");
  connection_noti_.WaitForNotification();
  gpr_log(GPR_DEBUG, "end waiting for noti");
  return std::move(other_end_binder_);
}

absl::Status WireReaderImpl::ProcessTransaction(transaction_code_t code,
                                                ReadableParcel* parcel,
                                                int uid) {
  if (code >= static_cast<unsigned>(kFirstCallId)) {
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

  {
    grpc_core::MutexLock lock(&mu_);
    if (static_cast<BinderTransportTxCode>(code) !=
            BinderTransportTxCode::SETUP_TRANSPORT &&
        !connected_) {
      return absl::InvalidArgumentError("Transports not connected yet");
    }
  }

  // TODO(mingcl): See if we want to check the security policy for every RPC
  // call or just during transport setup.

  switch (static_cast<BinderTransportTxCode>(code)) {
    case BinderTransportTxCode::SETUP_TRANSPORT: {
      grpc_core::MutexLock lock(&mu_);
      if (recvd_setup_transport_) {
        return absl::InvalidArgumentError(
            "Already received a SETUP_TRANSPORT request");
      }
      recvd_setup_transport_ = true;

      gpr_log(GPR_DEBUG, "calling uid = %d", uid);
      if (!security_policy_->IsAuthorized(uid)) {
        return absl::PermissionDeniedError(
            "UID " + std::to_string(uid) +
            " is not allowed to connect to this "
            "transport according to security policy.");
      }

      int version;
      GRPC_RETURN_IF_ERROR(parcel->ReadInt32(&version));
      gpr_log(GPR_DEBUG, "The other end respond with version = %d", version);
      // We only support this single lowest possible version, so server must
      // respond that version too.
      if (version != kWireFormatVersion) {
        gpr_log(GPR_ERROR,
                "The other end respond with version = %d, but we requested "
                "version %d, trying to continue anyway",
                version, kWireFormatVersion);
      }
      std::unique_ptr<Binder> binder{};
      GRPC_RETURN_IF_ERROR(parcel->ReadBinder(&binder));
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
      GRPC_RETURN_IF_ERROR(parcel->ReadInt64(&num_bytes));
      gpr_log(GPR_DEBUG, "received acknowledge bytes = %" PRId64, num_bytes);
      if (!wire_writer_ready_notification_.WaitForNotificationWithTimeout(
              absl::Seconds(5))) {
        return absl::DeadlineExceededError(
            "wire_writer_ is not ready in time!");
      }
      wire_writer_->OnAckReceived(num_bytes);
      break;
    }
    case BinderTransportTxCode::PING: {
      if (is_client_) {
        return absl::FailedPreconditionError("Receive PING request in client");
      }
      int ping_id = -1;
      GRPC_RETURN_IF_ERROR(parcel->ReadInt32(&ping_id));
      gpr_log(GPR_DEBUG, "received ping id = %d", ping_id);
      // TODO(waynetu): Ping back.
      break;
    }
    case BinderTransportTxCode::PING_RESPONSE: {
      int value = -1;
      GRPC_RETURN_IF_ERROR(parcel->ReadInt32(&value));
      gpr_log(GPR_DEBUG, "received ping response = %d", value);
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status WireReaderImpl::ProcessStreamingTransaction(
    transaction_code_t code, ReadableParcel* parcel) {
  bool need_to_send_ack = false;
  int64_t num_bytes = 0;
  // Indicates which callbacks should be cancelled. It will be initialized as
  // the flags the in-coming transaction carries, and when a particular
  // callback is completed, the corresponding bit in cancellation_flag will be
  // set to 0 so that we won't cancel it afterward.
  int cancellation_flags = 0;
  // The queue saves the actions needed to be done "WITHOUT" `mu_`.
  // It prevents deadlock against wire writer issues.
  std::queue<absl::AnyInvocable<void() &&>> deferred_func_queue;
  absl::Status tx_process_result;

  {
    grpc_core::MutexLock lock(&mu_);
    if (!connected_) {
      return absl::InvalidArgumentError("Transports not connected yet");
    }

    tx_process_result = ProcessStreamingTransactionImpl(
        code, parcel, &cancellation_flags, deferred_func_queue);
    if ((num_incoming_bytes_ - num_acknowledged_bytes_) >=
        kFlowControlAckBytes) {
      need_to_send_ack = true;
      num_bytes = num_incoming_bytes_;
      num_acknowledged_bytes_ = num_incoming_bytes_;
    }
  }
  // Executes all actions in the queue.
  while (!deferred_func_queue.empty()) {
    std::move(deferred_func_queue.front())();
    deferred_func_queue.pop();
  }

  if (!tx_process_result.ok()) {
    gpr_log(GPR_ERROR, "Failed to process streaming transaction: %s",
            tx_process_result.ToString().c_str());
    // Something went wrong when receiving transaction. Cancel failed requests.
    if (cancellation_flags & kFlagPrefix) {
      gpr_log(GPR_INFO, "cancelling initial metadata");
      transport_stream_receiver_->NotifyRecvInitialMetadata(code,
                                                            tx_process_result);
    }
    if (cancellation_flags & kFlagMessageData) {
      gpr_log(GPR_INFO, "cancelling message data");
      transport_stream_receiver_->NotifyRecvMessage(code, tx_process_result);
    }
    if (cancellation_flags & kFlagSuffix) {
      gpr_log(GPR_INFO, "cancelling trailing metadata");
      transport_stream_receiver_->NotifyRecvTrailingMetadata(
          code, tx_process_result, 0);
    }
  }

  if (need_to_send_ack) {
    if (!wire_writer_ready_notification_.WaitForNotificationWithTimeout(
            absl::Seconds(5))) {
      return absl::DeadlineExceededError("wire_writer_ is not ready in time!");
    }
    GPR_ASSERT(wire_writer_);
    // wire_writer_ should not be accessed while holding mu_!
    // Otherwise, it is possible that
    // 1. wire_writer_::mu_ is acquired before mu_ (NDK call back during
    // transaction)
    // 2. mu_ is acquired before wire_writer_::mu_ (e.g. Java call back us, and
    // we call WireWriter::SendAck which will try to acquire wire_writer_::mu_)
    absl::Status ack_status = wire_writer_->SendAck(num_bytes);
    if (tx_process_result.ok()) {
      return ack_status;
    }
  }
  return tx_process_result;
}

absl::Status WireReaderImpl::ProcessStreamingTransactionImpl(
    transaction_code_t code, ReadableParcel* parcel, int* cancellation_flags,
    std::queue<absl::AnyInvocable<void() &&>>& deferred_func_queue) {
  GPR_ASSERT(cancellation_flags);
  num_incoming_bytes_ += parcel->GetDataSize();
  gpr_log(GPR_INFO, "Total incoming bytes: %" PRId64, num_incoming_bytes_);

  int flags;
  GRPC_RETURN_IF_ERROR(parcel->ReadInt32(&flags));
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
  gpr_log(GPR_DEBUG, "status = %d", status);
  gpr_log(GPR_DEBUG, "FLAG_PREFIX = %d", (flags & kFlagPrefix));
  gpr_log(GPR_DEBUG, "FLAG_MESSAGE_DATA = %d", (flags & kFlagMessageData));
  gpr_log(GPR_DEBUG, "FLAG_SUFFIX = %d", (flags & kFlagSuffix));
  int seq_num;
  GRPC_RETURN_IF_ERROR(parcel->ReadInt32(&seq_num));
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
  gpr_log(GPR_DEBUG, "sequence number = %d", seq_num);
  if (flags & kFlagPrefix) {
    std::string method_ref;
    if (!is_client_) {
      GRPC_RETURN_IF_ERROR(parcel->ReadString(&method_ref));
    }
    absl::StatusOr<Metadata> initial_metadata_or_error = parse_metadata(parcel);
    if (!initial_metadata_or_error.ok()) {
      return initial_metadata_or_error.status();
    }
    if (!is_client_) {
      // In BinderChannel wireformat specification, path is not encoded as part
      // of metadata. So we extract the path and turn it into metadata here
      // (this is what core API layer expects).
      initial_metadata_or_error->emplace_back(":path",
                                              std::string("/") + method_ref);
      // Since authority metadata is not part of BinderChannel wireformat
      // specification, and gRPC core API layer expects the presence of
      // authority for message sent from client to server, we add one if
      // missing (it will be missing if client grpc-java).
      bool has_authority = false;
      for (const auto& p : *initial_metadata_or_error) {
        if (p.first == kAuthorityMetadataKey) has_authority = true;
      }
      if (!has_authority) {
        initial_metadata_or_error->emplace_back(kAuthorityMetadataKey,
                                                "binder.authority");
      }
    }
    deferred_func_queue.emplace([this, code,
                                 initial_metadata_or_error = std::move(
                                     initial_metadata_or_error)]() mutable {
      this->transport_stream_receiver_->NotifyRecvInitialMetadata(
          code, std::move(initial_metadata_or_error));
    });
    *cancellation_flags &= ~kFlagPrefix;
  }
  if (flags & kFlagMessageData) {
    int count;
    GRPC_RETURN_IF_ERROR(parcel->ReadInt32(&count));
    gpr_log(GPR_DEBUG, "count = %d", count);
    std::string msg_data{};
    if (count > 0) {
      GRPC_RETURN_IF_ERROR(parcel->ReadByteArray(&msg_data));
    }
    message_buffer_[code] += msg_data;
    if ((flags & kFlagMessageDataIsPartial) == 0) {
      std::string s = std::move(message_buffer_[code]);
      message_buffer_.erase(code);
      deferred_func_queue.emplace([this, code, s = std::move(s)]() mutable {
        this->transport_stream_receiver_->NotifyRecvMessage(code, std::move(s));
      });
    }
    *cancellation_flags &= ~kFlagMessageData;
  }
  if (flags & kFlagSuffix) {
    if (flags & kFlagStatusDescription) {
      // FLAG_STATUS_DESCRIPTION set
      std::string desc;
      GRPC_RETURN_IF_ERROR(parcel->ReadString(&desc));
      gpr_log(GPR_DEBUG, "description = %s", desc.c_str());
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
    deferred_func_queue.emplace(
        [this, code, trailing_metadata = std::move(trailing_metadata),
         status]() mutable {
          this->transport_stream_receiver_->NotifyRecvTrailingMetadata(
              code, std::move(trailing_metadata), status);
        });
    *cancellation_flags &= ~kFlagSuffix;
  }
  return absl::OkStatus();
}

}  // namespace grpc_binder
#endif
