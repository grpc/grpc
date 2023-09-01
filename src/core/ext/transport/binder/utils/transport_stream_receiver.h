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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_H

#include <grpc/support/port_platform.h>

#include <functional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

#include "src/core/ext/transport/binder/wire_format/transaction.h"

namespace grpc_binder {

typedef int StreamIdentifier;

class TransportStreamReceiver {
 public:
  virtual ~TransportStreamReceiver() = default;

  using InitialMetadataCallbackType =
      std::function<void(absl::StatusOr<Metadata>)>;
  using MessageDataCallbackType =
      std::function<void(absl::StatusOr<std::string>)>;
  using TrailingMetadataCallbackType =
      std::function<void(absl::StatusOr<Metadata>, int)>;

  // Only handles single time invocation. Callback object will be deleted.
  // The callback should be valid until invocation or unregister.
  virtual void RegisterRecvInitialMetadata(StreamIdentifier id,
                                           InitialMetadataCallbackType cb) = 0;
  virtual void RegisterRecvMessage(StreamIdentifier id,
                                   MessageDataCallbackType cb) = 0;
  virtual void RegisterRecvTrailingMetadata(
      StreamIdentifier id, TrailingMetadataCallbackType cb) = 0;

  // For the following functions, the second arguments are the transaction
  // result received from the lower level. If it is None, that means there's
  // something wrong when receiving the corresponding transaction. In such case,
  // we should cancel the gRPC callback as well.
  virtual void NotifyRecvInitialMetadata(
      StreamIdentifier id, absl::StatusOr<Metadata> initial_metadata) = 0;
  virtual void NotifyRecvMessage(StreamIdentifier id,
                                 absl::StatusOr<std::string> message) = 0;
  virtual void NotifyRecvTrailingMetadata(
      StreamIdentifier id, absl::StatusOr<Metadata> trailing_metadata,
      int status) = 0;
  // Remove all entries associated with stream number `id`.
  virtual void CancelStream(StreamIdentifier id) = 0;

  static const absl::string_view kGrpcBinderTransportCancelledGracefully;
};

}  // namespace grpc_binder

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_H
