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

#include "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.h"

#include <grpc/support/port_platform.h>

#ifndef GRPC_NO_BINDER

#include <functional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "src/core/lib/gprpp/crash.h"

namespace grpc_binder {

const absl::string_view
    TransportStreamReceiver::kGrpcBinderTransportCancelledGracefully =
        "grpc-binder-transport: cancelled gracefully";

void TransportStreamReceiverImpl::RegisterRecvInitialMetadata(
    StreamIdentifier id, InitialMetadataCallbackType cb) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  absl::StatusOr<Metadata> initial_metadata{};
  {
    grpc_core::MutexLock l(&m_);
    CHECK_EQ(initial_metadata_cbs_.count(id), 0u);
    auto iter = pending_initial_metadata_.find(id);
    if (iter == pending_initial_metadata_.end()) {
      if (trailing_metadata_recvd_.count(id)) {
        cb(absl::CancelledError(""));
      } else {
        initial_metadata_cbs_[id] = std::move(cb);
      }
      cb = nullptr;
    } else {
      initial_metadata = std::move(iter->second.front());
      iter->second.pop();
      if (iter->second.empty()) {
        pending_initial_metadata_.erase(iter);
      }
    }
  }
  if (cb != nullptr) {
    cb(std::move(initial_metadata));
  }
}

void TransportStreamReceiverImpl::RegisterRecvMessage(
    StreamIdentifier id, MessageDataCallbackType cb) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  absl::StatusOr<std::string> message{};
  {
    grpc_core::MutexLock l(&m_);
    CHECK_EQ(message_cbs_.count(id), 0u);
    auto iter = pending_message_.find(id);
    if (iter == pending_message_.end()) {
      // If we'd already received trailing-metadata and there's no pending
      // messages, cancel the callback.
      if (trailing_metadata_recvd_.count(id)) {
        cb(absl::CancelledError(
            TransportStreamReceiver::kGrpcBinderTransportCancelledGracefully));
      } else {
        message_cbs_[id] = std::move(cb);
      }
      cb = nullptr;
    } else {
      // We'll still keep all pending messages received before the trailing
      // metadata since they're issued before the end of stream, as promised by
      // WireReader which keeps transactions commit in-order.
      message = std::move(iter->second.front());
      iter->second.pop();
      if (iter->second.empty()) {
        pending_message_.erase(iter);
      }
    }
  }
  if (cb != nullptr) {
    cb(std::move(message));
  }
}

void TransportStreamReceiverImpl::RegisterRecvTrailingMetadata(
    StreamIdentifier id, TrailingMetadataCallbackType cb) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  std::pair<absl::StatusOr<Metadata>, int> trailing_metadata{};
  {
    grpc_core::MutexLock l(&m_);
    CHECK_EQ(trailing_metadata_cbs_.count(id), 0u);
    auto iter = pending_trailing_metadata_.find(id);
    if (iter == pending_trailing_metadata_.end()) {
      trailing_metadata_cbs_[id] = std::move(cb);
      cb = nullptr;
    } else {
      trailing_metadata = std::move(iter->second.front());
      iter->second.pop();
      if (iter->second.empty()) {
        pending_trailing_metadata_.erase(iter);
      }
    }
  }
  if (cb != nullptr) {
    cb(std::move(trailing_metadata.first), trailing_metadata.second);
  }
}

void TransportStreamReceiverImpl::NotifyRecvInitialMetadata(
    StreamIdentifier id, absl::StatusOr<Metadata> initial_metadata) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  if (!is_client_ && accept_stream_callback_ && initial_metadata.ok()) {
    accept_stream_callback_();
  }
  InitialMetadataCallbackType cb;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = initial_metadata_cbs_.find(id);
    if (iter != initial_metadata_cbs_.end()) {
      cb = iter->second;
      initial_metadata_cbs_.erase(iter);
    } else {
      pending_initial_metadata_[id].push(std::move(initial_metadata));
      return;
    }
  }
  cb(std::move(initial_metadata));
}

void TransportStreamReceiverImpl::NotifyRecvMessage(
    StreamIdentifier id, absl::StatusOr<std::string> message) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  MessageDataCallbackType cb;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = message_cbs_.find(id);
    if (iter != message_cbs_.end()) {
      cb = iter->second;
      message_cbs_.erase(iter);
    } else {
      pending_message_[id].push(std::move(message));
      return;
    }
  }
  cb(std::move(message));
}

void TransportStreamReceiverImpl::NotifyRecvTrailingMetadata(
    StreamIdentifier id, absl::StatusOr<Metadata> trailing_metadata,
    int status) {
  // Trailing metadata mark the end of the stream. Since TransportStreamReceiver
  // assumes in-order commitments of transactions and that trailing metadata is
  // parsed after message data, we can safely cancel all upcoming callbacks of
  // recv_message.
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  OnRecvTrailingMetadata(id);
  TrailingMetadataCallbackType cb;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = trailing_metadata_cbs_.find(id);
    if (iter != trailing_metadata_cbs_.end()) {
      cb = iter->second;
      trailing_metadata_cbs_.erase(iter);
    } else {
      pending_trailing_metadata_[id].emplace(std::move(trailing_metadata),
                                             status);
      return;
    }
  }
  cb(std::move(trailing_metadata), status);
}

void TransportStreamReceiverImpl::CancelInitialMetadataCallback(
    StreamIdentifier id, absl::Status error) {
  InitialMetadataCallbackType callback = nullptr;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = initial_metadata_cbs_.find(id);
    if (iter != initial_metadata_cbs_.end()) {
      callback = std::move(iter->second);
      initial_metadata_cbs_.erase(iter);
    }
  }
  if (callback != nullptr) {
    std::move(callback)(error);
  }
}

void TransportStreamReceiverImpl::CancelMessageCallback(StreamIdentifier id,
                                                        absl::Status error) {
  MessageDataCallbackType callback = nullptr;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = message_cbs_.find(id);
    if (iter != message_cbs_.end()) {
      callback = std::move(iter->second);
      message_cbs_.erase(iter);
    }
  }
  if (callback != nullptr) {
    std::move(callback)(error);
  }
}

void TransportStreamReceiverImpl::CancelTrailingMetadataCallback(
    StreamIdentifier id, absl::Status error) {
  TrailingMetadataCallbackType callback = nullptr;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = trailing_metadata_cbs_.find(id);
    if (iter != trailing_metadata_cbs_.end()) {
      callback = std::move(iter->second);
      trailing_metadata_cbs_.erase(iter);
    }
  }
  if (callback != nullptr) {
    std::move(callback)(error, 0);
  }
}

void TransportStreamReceiverImpl::OnRecvTrailingMetadata(StreamIdentifier id) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  m_.Lock();
  trailing_metadata_recvd_.insert(id);
  m_.Unlock();
  CancelInitialMetadataCallback(id, absl::CancelledError(""));
  CancelMessageCallback(
      id,
      absl::CancelledError(
          TransportStreamReceiver::kGrpcBinderTransportCancelledGracefully));
}

void TransportStreamReceiverImpl::CancelStream(StreamIdentifier id) {
  LOG(INFO) << __func__ << " id = " << id << " is_client = " << is_client_;
  CancelInitialMetadataCallback(id, absl::CancelledError("Stream cancelled"));
  CancelMessageCallback(id, absl::CancelledError("Stream cancelled"));
  CancelTrailingMetadataCallback(id, absl::CancelledError("Stream cancelled"));
  grpc_core::MutexLock l(&m_);
  trailing_metadata_recvd_.erase(id);
  pending_initial_metadata_.erase(id);
  pending_message_.erase(id);
  pending_trailing_metadata_.erase(id);
}
}  // namespace grpc_binder
#endif
