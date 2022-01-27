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

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/transaction.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

class WireWriter {
 public:
  virtual ~WireWriter() = default;
  virtual absl::Status RpcCall(const Transaction& call) = 0;
  virtual absl::Status SendAck(int64_t num_bytes) = 0;
  virtual void OnAckReceived(int64_t num_bytes) = 0;
};

class WireWriterImpl : public WireWriter {
 public:
  explicit WireWriterImpl(std::unique_ptr<Binder> binder);
  absl::Status RpcCall(const Transaction& tx) override;
  absl::Status SendAck(int64_t num_bytes) override;
  void OnAckReceived(int64_t num_bytes) override;

  // Split long message into chunks of size 16k. This doesn't necessarily have
  // to be the same as the flow control acknowledgement size, but it should not
  // exceed 128k.
  static const int64_t kBlockSize;
  // Flow control allows sending at most 128k between acknowledgements.
  static const int64_t kFlowControlWindowSize;

 private:
  absl::Status WriteInitialMetadata(const Transaction& tx,
                                    WritableParcel* parcel)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  absl::Status WriteTrailingMetadata(const Transaction& tx,
                                     WritableParcel* parcel)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  bool CanBeSentInOneTransaction(const Transaction& tx) const;
  absl::Status RpcCallFastPath(const Transaction& tx)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Wait for acknowledgement from the other side for a while (the timeout is
  // currently set to 10ms for debugability). Returns true if we are able to
  // proceed, and false otherwise.
  //
  // TODO(waynetu): Currently, RpcCall() will fail if we are blocked for 10ms.
  // In the future, we should queue the transactions and release them later when
  // acknowledgement comes.
  bool WaitForAcknowledgement() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  std::unique_ptr<Binder> binder_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<int, int> seq_num_ ABSL_GUARDED_BY(mu_);
  int64_t num_outgoing_bytes_ ABSL_GUARDED_BY(mu_) = 0;
  int64_t num_acknowledged_bytes_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H
