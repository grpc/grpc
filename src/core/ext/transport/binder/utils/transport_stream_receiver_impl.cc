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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/binder/utils/transport_stream_receiver_impl.h"

#include <grpc/support/log.h>

#include <functional>
#include <string>
#include <utility>

namespace grpc_binder {
void TransportStreamReceiverImpl::RegisterRecvInitialMetadata(
    StreamIdentifier id, std::function<void(const Metadata&)> cb) {
  // TODO(mingcl): Don't lock the whole function
  grpc_core::MutexLock l(&m_);
  gpr_log(GPR_ERROR, "%s id = %d", __func__, id);
  GPR_ASSERT(initial_metadata_cbs_.count(id) == 0);
  auto iter = pending_initial_metadata_.find(id);
  if (iter == pending_initial_metadata_.end()) {
    initial_metadata_cbs_[id] = std::move(cb);
  } else {
    cb(iter->second.front());
    iter->second.pop();
    if (iter->second.empty()) {
      pending_initial_metadata_.erase(iter);
    }
  }
}

void TransportStreamReceiverImpl::RegisterRecvMessage(
    StreamIdentifier id, std::function<void(const std::string&)> cb) {
  // TODO(mingcl): Don't lock the whole function
  grpc_core::MutexLock l(&m_);
  gpr_log(GPR_ERROR, "%s id = %d", __func__, id);
  GPR_ASSERT(message_cbs_.count(id) == 0);
  auto iter = pending_message_.find(id);
  if (iter == pending_message_.end()) {
    message_cbs_[id] = std::move(cb);
  } else {
    cb(iter->second.front());
    iter->second.pop();
    if (iter->second.empty()) {
      pending_message_.erase(iter);
    }
  }
}

void TransportStreamReceiverImpl::RegisterRecvTrailingMetadata(
    StreamIdentifier id, std::function<void(const Metadata&, int)> cb) {
  // TODO(mingcl): Don't lock the whole function
  grpc_core::MutexLock l(&m_);
  gpr_log(GPR_ERROR, "%s id = %d", __func__, id);
  GPR_ASSERT(trailing_metadata_cbs_.count(id) == 0);
  auto iter = pending_trailing_metadata_.find(id);
  if (iter == pending_trailing_metadata_.end()) {
    trailing_metadata_cbs_[id] = std::move(cb);
  } else {
    {
      const auto& p = iter->second.front();
      cb(p.first, p.second);
    }
    iter->second.pop();
    if (iter->second.empty()) {
      pending_trailing_metadata_.erase(iter);
    }
  }
}

void TransportStreamReceiverImpl::NotifyRecvInitialMetadata(
    StreamIdentifier id, const Metadata& initial_metadata) {
  gpr_log(GPR_ERROR, "%s id = %d", __func__, id);
  std::function<void(const Metadata&)> cb;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = initial_metadata_cbs_.find(id);
    if (iter != initial_metadata_cbs_.end()) {
      cb = iter->second;
      initial_metadata_cbs_.erase(iter);
    } else {
      pending_initial_metadata_[id].push(initial_metadata);
    }
  }
  if (cb != nullptr) {
    cb(initial_metadata);
  }
}

void TransportStreamReceiverImpl::NotifyRecvMessage(
    StreamIdentifier id, const std::string& message) {
  gpr_log(GPR_ERROR, "%s id = %d", __func__, id);
  std::function<void(const std::string&)> cb;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = message_cbs_.find(id);
    if (iter != message_cbs_.end()) {
      cb = iter->second;
      message_cbs_.erase(iter);
    } else {
      pending_message_[id].push(message);
    }
  }
  if (cb != nullptr) {
    cb(message);
  }
}

void TransportStreamReceiverImpl::NotifyRecvTrailingMetadata(
    StreamIdentifier id, const Metadata& trailing_metadata, int status) {
  gpr_log(GPR_ERROR, "%s id = %d", __func__, id);
  std::function<void(const Metadata&, int)> cb;
  {
    grpc_core::MutexLock l(&m_);
    auto iter = trailing_metadata_cbs_.find(id);
    if (iter != trailing_metadata_cbs_.end()) {
      cb = iter->second;
      trailing_metadata_cbs_.erase(iter);
    } else {
      pending_trailing_metadata_[id].emplace(trailing_metadata, status);
    }
  }
  if (cb != nullptr) {
    cb(trailing_metadata, status);
  }
}
}  // namespace grpc_binder
