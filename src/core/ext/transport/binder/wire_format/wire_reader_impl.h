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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_READER_IMPL_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_READER_IMPL_H

#include <grpc/impl/codegen/port_platform.h>

#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/notification.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

namespace grpc_binder {

class WireReaderImpl : public WireReader {
 public:
  explicit WireReaderImpl(TransportStreamReceiver* transport_stream_receiver,
                          bool is_client);
  ~WireReaderImpl() override;

  // Setup the transport between endpoint binders.
  //
  // The client and the server both call SetupTransport() when constructing
  // transport.
  //
  // High-level overview of transaction setup:
  // 0. Client obtain an |endpoint_binder| from the server.
  // 1. Client creates a binder |client_binder| and hook its on-transaction
  // callback to client's ProcessTransaction(). Client then sends
  // |client_binder| through |endpoint_binder| to server.
  // 2. Server receives |client_binder| via |endpoint_binder|.
  // 3. Server creates a binder |server_binder| and hook its on-transaction
  // callback to server's ProcessTransaction(). Server then sends
  // |server_binder| through |client_binder| back to the client.
  // 4. Client receives |server_binder| via |client_binder|'s on-transaction
  // callback.
  std::unique_ptr<WireWriter> SetupTransport(
      std::unique_ptr<Binder> endpoint_binder) override;

  absl::Status ProcessTransaction(transaction_code_t code,
                                  const ReadableParcel* parcel);

 private:
  void SendSetupTransport(Binder* binder);
  void RecvSetupTransport();
  absl::Status ProcessStreamingTransaction(transaction_code_t code,
                                           const ReadableParcel* parcel);

  TransportStreamReceiver* transport_stream_receiver_;
  absl::Notification connection_noti_;
  std::unique_ptr<Binder> other_end_binder_;
  absl::flat_hash_map<transaction_code_t, int32_t> expected_seq_num_;
  std::unique_ptr<TransactionReceiver> tx_receiver_;
  bool is_client_;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_READER_IMPL_H
