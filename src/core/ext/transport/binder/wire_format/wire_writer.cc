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

#include "absl/cleanup/cleanup.h"
#include "absl/types/variant.h"

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/crash.h"

#define RETURN_IF_ERROR(expr)           \
  do {                                  \
    const absl::Status status = (expr); \
    if (!status.ok()) return status;    \
  } while (0)

namespace grpc_binder {

bool CanBeSentInOneTransaction(const Transaction& tx) {
  return (tx.GetFlags() & kFlagMessageData) == 0 ||
         static_cast<int64_t>(tx.GetMessageData().size()) <=
             WireWriterImpl::kBlockSize;
}

// Simply forward the call to `WireWriterImpl::RunScheduledTx`.
void RunScheduledTx(void* arg, grpc_error_handle /*error*/) {
  auto* run_scheduled_tx_args =
      static_cast<WireWriterImpl::RunScheduledTxArgs*>(arg);
  run_scheduled_tx_args->writer->RunScheduledTxInternal(run_scheduled_tx_args);
}

absl::Status WriteInitialMetadata(const Transaction& tx,
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

absl::Status WriteTrailingMetadata(const Transaction& tx,
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

WireWriterImpl::WireWriterImpl(std::unique_ptr<Binder> binder)
    : binder_(std::move(binder)),
      combiner_(grpc_combiner_create(
          grpc_event_engine::experimental::GetDefaultEventEngine())) {}

WireWriterImpl::~WireWriterImpl() {
  GRPC_COMBINER_UNREF(combiner_, "wire_writer_impl");
  while (!pending_outgoing_tx_.empty()) {
    delete pending_outgoing_tx_.front();
    pending_outgoing_tx_.pop();
  }
}

// Flow control constant are specified at
// https://github.com/grpc/proposal/blob/master/L73-java-binderchannel/wireformat.md#flow-control
const int64_t WireWriterImpl::kBlockSize = 16 * 1024;
const int64_t WireWriterImpl::kFlowControlWindowSize = 128 * 1024;

absl::Status WireWriterImpl::MakeBinderTransaction(
    BinderTransportTxCode tx_code,
    std::function<absl::Status(WritableParcel*)> fill_parcel) {
  grpc_core::MutexLock lock(&write_mu_);
  RETURN_IF_ERROR(binder_->PrepareTransaction());
  WritableParcel* parcel = binder_->GetWritableParcel();
  RETURN_IF_ERROR(fill_parcel(parcel));
  // Only stream transaction is accounted in flow control spec.
  if (static_cast<int32_t>(tx_code) >= kFirstCallId) {
    int64_t parcel_size = parcel->GetDataSize();
    if (parcel_size > 2 * kBlockSize) {
      gpr_log(GPR_ERROR,
              "Unexpected large transaction (possibly caused by a very large "
              "metadata). This might overflow the binder "
              "transaction buffer. Size: %" PRId64 " bytes",
              parcel_size);
    }
    num_outgoing_bytes_ += parcel_size;
    gpr_log(GPR_INFO, "Total outgoing bytes: %" PRId64,
            num_outgoing_bytes_.load());
  }
  GPR_ASSERT(!is_transacting_);
  is_transacting_ = true;
  absl::Status result = binder_->Transact(tx_code);
  is_transacting_ = false;
  return result;
}

absl::Status WireWriterImpl::RpcCallFastPath(std::unique_ptr<Transaction> tx) {
  return MakeBinderTransaction(
      static_cast<BinderTransportTxCode>(tx->GetTxCode()),
      [this, tx = tx.get()](
          WritableParcel* parcel) ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
        RETURN_IF_ERROR(parcel->WriteInt32(tx->GetFlags()));
        RETURN_IF_ERROR(parcel->WriteInt32(next_seq_num_[tx->GetTxCode()]++));
        if (tx->GetFlags() & kFlagPrefix) {
          RETURN_IF_ERROR(WriteInitialMetadata(*tx, parcel));
        }
        if (tx->GetFlags() & kFlagMessageData) {
          RETURN_IF_ERROR(
              parcel->WriteByteArrayWithLength(tx->GetMessageData()));
        }
        if (tx->GetFlags() & kFlagSuffix) {
          RETURN_IF_ERROR(WriteTrailingMetadata(*tx, parcel));
        }
        return absl::OkStatus();
      });
}

absl::Status WireWriterImpl::RunStreamTx(
    RunScheduledTxArgs::StreamTx* stream_tx, WritableParcel* parcel,
    bool* is_last_chunk) {
  Transaction* tx = stream_tx->tx.get();
  // Transaction without data flag should go to fast path.
  GPR_ASSERT(tx->GetFlags() & kFlagMessageData);

  absl::string_view data = tx->GetMessageData();
  GPR_ASSERT(stream_tx->bytes_sent <= static_cast<int64_t>(data.size()));

  int flags = kFlagMessageData;

  if (stream_tx->bytes_sent == 0) {
    // This is the first transaction. Include initial
    // metadata if there's any.
    if (tx->GetFlags() & kFlagPrefix) {
      flags |= kFlagPrefix;
    }
  }
  // There is also prefix/suffix in transaction beside the transaction data so
  // actual transaction size will be greater than `kBlockSize`. This is
  // unavoidable because we cannot split the prefix metadata and trailing
  // metadata into different binder transactions. In most cases this is fine
  // because single transaction size is not required to be strictly lower than
  // `kBlockSize`, as long as it won't overflow Android's binder buffer.
  int64_t size = std::min<int64_t>(WireWriterImpl::kBlockSize,
                                   data.size() - stream_tx->bytes_sent);
  if (stream_tx->bytes_sent + WireWriterImpl::kBlockSize >=
      static_cast<int64_t>(data.size())) {
    // This is the last transaction. Include trailing
    // metadata if there's any.
    if (tx->GetFlags() & kFlagSuffix) {
      flags |= kFlagSuffix;
    }
    size = data.size() - stream_tx->bytes_sent;
    *is_last_chunk = true;
  } else {
    // There are more messages to send.
    flags |= kFlagMessageDataIsPartial;
    *is_last_chunk = false;
  }
  RETURN_IF_ERROR(parcel->WriteInt32(flags));
  RETURN_IF_ERROR(parcel->WriteInt32(next_seq_num_[tx->GetTxCode()]++));
  if (flags & kFlagPrefix) {
    RETURN_IF_ERROR(WriteInitialMetadata(*tx, parcel));
  }
  RETURN_IF_ERROR(parcel->WriteByteArrayWithLength(
      data.substr(stream_tx->bytes_sent, size)));
  if (flags & kFlagSuffix) {
    RETURN_IF_ERROR(WriteTrailingMetadata(*tx, parcel));
  }
  stream_tx->bytes_sent += size;
  return absl::OkStatus();
}

void WireWriterImpl::RunScheduledTxInternal(RunScheduledTxArgs* args) {
  GPR_ASSERT(args->writer == this);
  if (absl::holds_alternative<RunScheduledTxArgs::AckTx>(args->tx)) {
    int64_t num_bytes =
        absl::get<RunScheduledTxArgs::AckTx>(args->tx).num_bytes;
    absl::Status result =
        MakeBinderTransaction(BinderTransportTxCode::ACKNOWLEDGE_BYTES,
                              [num_bytes](WritableParcel* parcel) {
                                RETURN_IF_ERROR(parcel->WriteInt64(num_bytes));
                                return absl::OkStatus();
                              });
    if (!result.ok()) {
      gpr_log(GPR_ERROR, "Failed to make binder transaction %s",
              result.ToString().c_str());
    }
    delete args;
    return;
  }
  GPR_ASSERT(absl::holds_alternative<RunScheduledTxArgs::StreamTx>(args->tx));
  RunScheduledTxArgs::StreamTx* stream_tx =
      &absl::get<RunScheduledTxArgs::StreamTx>(args->tx);
  // Be reservative. Decrease CombinerTxCount after the data size of this
  // transaction has already been added to `num_outgoing_bytes_`, to make sure
  // we never underestimate `num_outgoing_bytes_`.
  auto decrease_combiner_tx_count = absl::MakeCleanup([this]() {
    {
      grpc_core::MutexLock lock(&flow_control_mu_);
      GPR_ASSERT(num_non_acked_tx_in_combiner_ > 0);
      num_non_acked_tx_in_combiner_--;
    }
    // New transaction might be ready to be scheduled.
    TryScheduleTransaction();
  });
  if (CanBeSentInOneTransaction(*stream_tx->tx.get())) {  // NOLINT
    absl::Status result = RpcCallFastPath(std::move(stream_tx->tx));
    if (!result.ok()) {
      gpr_log(GPR_ERROR, "Failed to handle non-chunked RPC call %s",
              result.ToString().c_str());
    }
    delete args;
    return;
  }
  bool is_last_chunk = true;
  absl::Status result = MakeBinderTransaction(
      static_cast<BinderTransportTxCode>(stream_tx->tx->GetTxCode()),
      [stream_tx, &is_last_chunk, this](WritableParcel* parcel)
          ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_) {
            return RunStreamTx(stream_tx, parcel, &is_last_chunk);
          });
  if (!result.ok()) {
    gpr_log(GPR_ERROR, "Failed to make binder transaction %s",
            result.ToString().c_str());
  }
  if (!is_last_chunk) {
    {
      grpc_core::MutexLock lock(&flow_control_mu_);
      pending_outgoing_tx_.push(args);
    }
    TryScheduleTransaction();
  } else {
    delete args;
  }
}

absl::Status WireWriterImpl::RpcCall(std::unique_ptr<Transaction> tx) {
  // TODO(mingcl): check tx_code <= last call id
  GPR_ASSERT(tx->GetTxCode() >= kFirstCallId);
  auto args = new RunScheduledTxArgs();
  args->writer = this;
  args->tx = RunScheduledTxArgs::StreamTx();
  absl::get<RunScheduledTxArgs::StreamTx>(args->tx).tx = std::move(tx);
  absl::get<RunScheduledTxArgs::StreamTx>(args->tx).bytes_sent = 0;
  {
    grpc_core::MutexLock lock(&flow_control_mu_);
    pending_outgoing_tx_.push(args);
  }
  TryScheduleTransaction();
  return absl::OkStatus();
}

absl::Status WireWriterImpl::SendAck(int64_t num_bytes) {
  // Ensure combiner will be run if this is not called from top-level gRPC API
  // entrypoint.
  grpc_core::ExecCtx exec_ctx;
  gpr_log(GPR_INFO, "Ack %" PRId64 " bytes received", num_bytes);
  if (is_transacting_) {
    // This can happen because NDK might call our registered callback function
    // in the same thread while we are telling it to send a transaction
    // `is_transacting_` will be true. `Binder::Transact` is now being called on
    // the same thread or the other thread. We are currently in the call stack
    // of other transaction, Liveness of ACK is still guaranteed even if this is
    // a race with another thread.
    gpr_log(
        GPR_INFO,
        "Scheduling ACK transaction instead of directly execute it to avoid "
        "deadlock.");
    auto args = new RunScheduledTxArgs();
    args->writer = this;
    args->tx = RunScheduledTxArgs::AckTx();
    absl::get<RunScheduledTxArgs::AckTx>(args->tx).num_bytes = num_bytes;
    auto cl = GRPC_CLOSURE_CREATE(RunScheduledTx, args, nullptr);
    combiner_->Run(cl, absl::OkStatus());
    return absl::OkStatus();
  }
  // Otherwise, we can directly send ack.
  absl::Status result =
      MakeBinderTransaction((BinderTransportTxCode::ACKNOWLEDGE_BYTES),
                            [num_bytes](WritableParcel* parcel) {
                              RETURN_IF_ERROR(parcel->WriteInt64(num_bytes));
                              return absl::OkStatus();
                            });
  if (!result.ok()) {
    gpr_log(GPR_ERROR, "Failed to make binder transaction %s",
            result.ToString().c_str());
  }
  return result;
}

void WireWriterImpl::OnAckReceived(int64_t num_bytes) {
  // Ensure combiner will be run if this is not called from top-level gRPC API
  // entrypoint.
  grpc_core::ExecCtx exec_ctx;
  gpr_log(GPR_INFO, "OnAckReceived %" PRId64, num_bytes);
  // Do not try to obtain `write_mu_` in this function. NDKBinder might invoke
  // the callback to notify us about new incoming binder transaction when we are
  // sending transaction. i.e. `write_mu_` might have already been acquired by
  // this thread.
  {
    grpc_core::MutexLock lock(&flow_control_mu_);
    num_acknowledged_bytes_ = std::max(num_acknowledged_bytes_, num_bytes);
    int64_t num_outgoing_bytes = num_outgoing_bytes_;
    if (num_acknowledged_bytes_ > num_outgoing_bytes) {
      gpr_log(GPR_ERROR,
              "The other end of transport acked more bytes than we ever sent, "
              "%" PRId64 " > %" PRId64,
              num_acknowledged_bytes_, num_outgoing_bytes);
    }
  }
  TryScheduleTransaction();
}

void WireWriterImpl::TryScheduleTransaction() {
  while (true) {
    grpc_core::MutexLock lock(&flow_control_mu_);
    if (pending_outgoing_tx_.empty()) {
      // Nothing to be schduled.
      break;
    }
    // Number of bytes we have scheduled in combiner but have not yet be
    // executed by combiner. Here we make an assumption that every binder
    // transaction will take `kBlockSize`. This should be close to truth when a
    // large message is being cut to `kBlockSize` chunks.
    int64_t num_bytes_scheduled_in_combiner =
        num_non_acked_tx_in_combiner_ * kBlockSize;
    // An estimation of number of bytes of traffic we will eventually send to
    // the other end, assuming all tasks in combiner will be executed and we
    // receive no new ACK from the other end of transport.
    int64_t num_total_bytes_will_be_sent =
        num_outgoing_bytes_ + num_bytes_scheduled_in_combiner;
    // An estimation of number of bytes of traffic that will not be
    // acknowledged, assuming all tasks in combiner will be executed and we
    // receive no new ack message fomr the other end of transport.
    int64_t num_non_acked_bytes_estimation =
        num_total_bytes_will_be_sent - num_acknowledged_bytes_;
    if (num_non_acked_bytes_estimation < 0) {
      gpr_log(
          GPR_ERROR,
          "Something went wrong. `num_non_acked_bytes_estimation` should be "
          "non-negative but it is %" PRId64,
          num_non_acked_bytes_estimation);
    }
    // If we can schedule another transaction (which has size estimation of
    // `kBlockSize`) without exceeding `kFlowControlWindowSize`, schedule it.
    if ((num_non_acked_bytes_estimation + kBlockSize <
         kFlowControlWindowSize)) {
      num_non_acked_tx_in_combiner_++;
      combiner_->Run(GRPC_CLOSURE_CREATE(RunScheduledTx,
                                         pending_outgoing_tx_.front(), nullptr),
                     absl::OkStatus());
      pending_outgoing_tx_.pop();
    } else {
      // It is common to fill `kFlowControlWindowSize` completely because
      // transactions are send at faster rate than the other end of transport
      // can handle it, so here we use `GPR_DEBUG` log level.
      gpr_log(GPR_DEBUG,
              "Some work cannot be scheduled yet due to slow ack from the "
              "other end of transport. This transport might be blocked if this "
              "number don't go down. pending_outgoing_tx_.size() = %zu "
              "pending_outgoing_tx_.front() = %p",
              pending_outgoing_tx_.size(), pending_outgoing_tx_.front());
      break;
    }
  }
}

}  // namespace grpc_binder

#endif
