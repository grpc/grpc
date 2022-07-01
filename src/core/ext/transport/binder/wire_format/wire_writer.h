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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H

#include <grpc/support/port_platform.h>

#include <queue>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/transaction.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/combiner.h"

namespace grpc_binder {

// Member functions are thread safe.
class WireWriter {
 public:
  virtual ~WireWriter() = default;
  virtual absl::Status RpcCall(std::unique_ptr<Transaction> tx) = 0;
  virtual absl::Status SendAck(int64_t num_bytes) = 0;
  virtual void OnAckReceived(int64_t num_bytes) = 0;
};

class WireWriterImpl : public WireWriter {
 public:
  explicit WireWriterImpl(std::unique_ptr<Binder> binder);
  ~WireWriterImpl() override;
  absl::Status RpcCall(std::unique_ptr<Transaction> tx) override;
  absl::Status SendAck(int64_t num_bytes) override;
  void OnAckReceived(int64_t num_bytes) override;

  // Required to be public because we would like to call this in combiner (which
  // cannot invoke class member function). `RunScheduledTxArgs` and
  // `RunScheduledTxInternal` should not be used by user directly.
  struct RunScheduledTxArgs {
    WireWriterImpl* writer;
    struct StreamTx {
      std::unique_ptr<Transaction> tx;
      // How many data in transaction's `data` field has been sent.
      int64_t bytes_sent = 0;
    };
    struct AckTx {
      int64_t num_bytes;
    };
    absl::variant<AckTx, StreamTx> tx;
  };

  void RunScheduledTxInternal(RunScheduledTxArgs* arg);

  // Split long message into chunks of size 16k. This doesn't necessarily have
  // to be the same as the flow control acknowledgement size, but it should not
  // exceed 128k.
  static const int64_t kBlockSize;

  // Flow control allows sending at most 128k between acknowledgements.
  static const int64_t kFlowControlWindowSize;

 private:
  // Fast path: send data in one transaction.
  absl::Status RpcCallFastPath(std::unique_ptr<Transaction> tx);

  // This function will acquire `write_mu_` to make sure the binder is not used
  // concurrently, so this can be called by different threads safely.
  absl::Status MakeBinderTransaction(
      BinderTransportTxCode tx_code,
      std::function<absl::Status(WritableParcel*)> fill_parcel);

  // Send a stream to `binder_`. Set `is_last_chunk` to `true` if the stream
  // transaction has been sent completely. Otherwise set to `false`.
  absl::Status RunStreamTx(RunScheduledTxArgs::StreamTx* stream_tx,
                           WritableParcel* parcel, bool* is_last_chunk)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(write_mu_);

  // Schdule `RunScheduledTxArgs*` in `pending_outgoing_tx_` to `combiner_`, as
  // many as possible (under the constraint of `kFlowControlWindowSize`).
  void TryScheduleTransaction();

  // Guards variables related to transport state.
  grpc_core::Mutex write_mu_;
  std::unique_ptr<Binder> binder_ ABSL_GUARDED_BY(write_mu_);

  // Maps the transaction code (which identifies streams) to their next
  // available sequence number. See
  // https://github.com/grpc/proposal/blob/master/L73-java-binderchannel/wireformat.md#sequence-number
  absl::flat_hash_map<int, int> next_seq_num_ ABSL_GUARDED_BY(write_mu_);

  // Number of bytes we have already sent in stream transactions.
  std::atomic<int64_t> num_outgoing_bytes_{0};

  // Guards variables related to flow control logic.
  grpc_core::Mutex flow_control_mu_;
  int64_t num_acknowledged_bytes_ ABSL_GUARDED_BY(flow_control_mu_) = 0;

  // The queue takes ownership of the pointer.
  std::queue<RunScheduledTxArgs*> pending_outgoing_tx_
      ABSL_GUARDED_BY(flow_control_mu_);
  int num_non_acked_tx_in_combiner_ ABSL_GUARDED_BY(flow_control_mu_) = 0;

  // Helper variable for determining if we are currently calling into
  // `Binder::Transact`. Useful for avoiding the attempt of acquiring
  // `write_mu_` multiple times on the same thread.
  std::atomic_bool is_transacting_{false};

  grpc_core::Combiner* combiner_;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H
