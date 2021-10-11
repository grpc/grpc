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

#include <grpc/support/port_platform.h>

#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

// Routes the data received from transport to corresponding streams
class TransportStreamReceiverImpl : public TransportStreamReceiver {
 public:
  explicit TransportStreamReceiverImpl(
      bool is_client, std::function<void()> accept_stream_callback = nullptr)
      : is_client_(is_client),
        accept_stream_callback_(accept_stream_callback) {}
  void RegisterRecvInitialMetadata(StreamIdentifier id,
                                   InitialMetadataCallbackType cb) override;
  void RegisterRecvMessage(StreamIdentifier id,
                           MessageDataCallbackType cb) override;
  void RegisterRecvTrailingMetadata(StreamIdentifier id,
                                    TrailingMetadataCallbackType cb) override;
  void NotifyRecvInitialMetadata(
      StreamIdentifier id, absl::StatusOr<Metadata> initial_metadata) override;
  void NotifyRecvMessage(StreamIdentifier id,
                         absl::StatusOr<std::string> message) override;
  void NotifyRecvTrailingMetadata(StreamIdentifier id,
                                  absl::StatusOr<Metadata> trailing_metadata,
                                  int status) override;

  void CancelStream(StreamIdentifier id) override;

 private:
  // Trailing metadata marks the end of one-side of the stream. Thus, after
  // receiving trailing metadata from the other-end, we know that there will
  // never be in-coming message data anymore, and all recv_message callbacks
  // (as well as recv_initial_metadata callback, if there's any) registered will
  // never be satisfied. This function cancels all such callbacks gracefully
  // (with GRPC_ERROR_NONE) to avoid being blocked waiting for them.
  void OnRecvTrailingMetadata(StreamIdentifier id);

  void CancelInitialMetadataCallback(StreamIdentifier id, absl::Status error);
  void CancelMessageCallback(StreamIdentifier id, absl::Status error);
  void CancelTrailingMetadataCallback(StreamIdentifier id, absl::Status error);

  std::map<StreamIdentifier, InitialMetadataCallbackType> initial_metadata_cbs_;
  std::map<StreamIdentifier, MessageDataCallbackType> message_cbs_;
  std::map<StreamIdentifier, TrailingMetadataCallbackType>
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
  //
  // TODO(waynetu): Use absl::flat_hash_map.
  std::map<StreamIdentifier, std::queue<absl::StatusOr<Metadata>>>
      pending_initial_metadata_ ABSL_GUARDED_BY(m_);
  std::map<StreamIdentifier, std::queue<absl::StatusOr<std::string>>>
      pending_message_ ABSL_GUARDED_BY(m_);
  std::map<StreamIdentifier,
           std::queue<std::pair<absl::StatusOr<Metadata>, int>>>
      pending_trailing_metadata_ ABSL_GUARDED_BY(m_);
  // Record whether or not the recv_message callbacks of a given stream is
  // cancelled. Although we explicitly cancel the registered recv_message() in
  // CancelRecvMessageCallbacksDueToTrailingMetadata(), there are chances that
  // the registration comes "after" we receive trailing metadata. Therefore,
  // when RegisterRecvMessage() gets called, we should check whether
  // recv_message_cancelled_ contains the corresponding stream ID, and if so,
  // directly cancel the callback gracefully without pending it.
  std::set<StreamIdentifier> trailing_metadata_recvd_ ABSL_GUARDED_BY(m_);

  bool is_client_;
  // Called when receiving initial metadata to inform the server about a new
  // stream.
  std::function<void()> accept_stream_callback_;
};
}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_IMPL_H
