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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_IMPL_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_IMPL_H

#include <grpc/impl/codegen/port_platform.h>

#include <functional>
#include <map>
#include <queue>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

// Routes the data received from transport to corresponding streams
class TransportStreamReceiverImpl : public TransportStreamReceiver {
 public:
  void RegisterRecvInitialMetadata(
      StreamIdentifier id, std::function<void(const Metadata&)> cb) override;
  void RegisterRecvMessage(StreamIdentifier id,
                           std::function<void(const std::string&)> cb) override;
  void RegisterRecvTrailingMetadata(
      StreamIdentifier id,
      std::function<void(const Metadata&, int)> cb) override;
  void NotifyRecvInitialMetadata(StreamIdentifier id,
                                 const Metadata& initial_metadata) override;
  void NotifyRecvMessage(StreamIdentifier id,
                         const std::string& message) override;
  void NotifyRecvTrailingMetadata(StreamIdentifier id,
                                  const Metadata& trailing_metadata,
                                  int status) override;

 private:
  std::map<StreamIdentifier, std::function<void(const Metadata&)>>
      initial_metadata_cbs_;
  std::map<StreamIdentifier, std::function<void(const std::string&)>>
      message_cbs_;
  std::map<StreamIdentifier, std::function<void(const Metadata&, int)>>
      trailing_metadata_cbs_;
  // TODO(waynetu): Better thread safety design. For example, use separate
  // mutexes for different type of messages.
  grpc_core::Mutex m_;
  // TODO(waynetu): gRPC surface layer will not wait for the current message to
  // be delivered before sending the next message. The following implementation
  // is still buggy with the current implementation of wire writer if
  // transaction issued first completes after the one issued later does. This is
  // because we just take the first element out of the queue and assume it's the
  // one issued first without further checking, which results in callbacks being
  // invoked with incorrect data.
  //
  // This should be fixed in the wire writer level and make sure out-of-order
  // messages will be re-ordered by it. In such case, the queueing approach will
  // work fine. Refer to the TODO in WireWriterImpl::ProcessTransaction() at
  // wire_reader_impl.cc for detecting and resolving out-of-order transactions.
  std::map<StreamIdentifier, std::queue<Metadata>> pending_initial_metadata_
      ABSL_GUARDED_BY(m_);
  std::map<StreamIdentifier, std::queue<std::string>> pending_message_
      ABSL_GUARDED_BY(m_);
  std::map<StreamIdentifier, std::queue<std::pair<Metadata, int>>>
      pending_trailing_metadata_ ABSL_GUARDED_BY(m_);
};
}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_IMPL_H
