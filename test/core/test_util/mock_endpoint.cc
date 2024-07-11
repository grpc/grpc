//
//
// Copyright 2016 gRPC authors.
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
//
//

#include "test/core/test_util/mock_endpoint.h"

#include <memory>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/event_engine_shims/endpoint.h"

namespace grpc_event_engine {
namespace experimental {

MockEndpoint::MockEndpoint(std::shared_ptr<EventEngine> engine)
    : engine_(std::move(engine)),
      peer_addr_(URIToResolvedAddress("ipv4:127.0.0.1:12345").value()),
      local_addr_(URIToResolvedAddress("ipv4:127.0.0.1:6789").value()) {}

MockEndpoint::~MockEndpoint() {
  grpc_core::MutexLock lock(&mu_);
  if (on_read_) {
    engine_->Run([cb = std::move(on_read_)]() mutable {
      cb(absl::InternalError("Endpoint Shutdown"));
    });
    on_read_ = nullptr;
  }
}

void MockEndpoint::TriggerReadEvent(Slice read_data) {
  grpc_core::MutexLock lock(&mu_);
  CHECK(!reads_done_)
      << "Cannot trigger a read event after NoMoreReads has been called.";
  if (on_read_) {
    on_read_slice_buffer_->Append(std::move(read_data));
    engine_->Run(
        [cb = std::move(on_read_)]() mutable { cb(absl::OkStatus()); });
    on_read_ = nullptr;
    on_read_slice_buffer_ = nullptr;
  } else {
    read_buffer_.Append(std::move(read_data));
  }
}

void MockEndpoint::NoMoreReads() {
  grpc_core::MutexLock lock(&mu_);
  CHECK(!std::exchange(reads_done_, true))
      << "NoMoreReads() can only be called once";
}

bool MockEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                        SliceBuffer* buffer, const ReadArgs* /* args */) {
  grpc_core::MutexLock lock(&mu_);
  if (read_buffer_.Count() > 0) {
    CHECK(buffer->Count() == 0);
    CHECK(!on_read_);
    read_buffer_.Swap(*buffer);
    engine_->Run([cb = std::move(on_read)]() mutable { cb(absl::OkStatus()); });
  } else if (reads_done_) {
    engine_->Run([cb = std::move(on_read)]() mutable {
      cb(absl::UnavailableError("reads done"));
    });
  } else {
    on_read_ = std::move(on_read);
    on_read_slice_buffer_ = buffer;
  }
  return false;
}

bool MockEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_writable,
                         SliceBuffer* data, const WriteArgs* /* args */) {
  // No-op implementation. Nothing was using it.
  data->Clear();
  engine_->Run(
      [cb = std::move(on_writable)]() mutable { cb(absl::OkStatus()); });
  return false;
}

const EventEngine::ResolvedAddress& MockEndpoint::GetPeerAddress() const {
  return peer_addr_;
}

const EventEngine::ResolvedAddress& MockEndpoint::GetLocalAddress() const {
  return local_addr_;
}

}  // namespace experimental
}  // namespace grpc_event_engine

grpc_endpoint* grpc_mock_endpoint_create(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine) {
  return grpc_event_engine_endpoint_create(
      std::make_unique<grpc_event_engine::experimental::MockEndpoint>(
          std::move(engine)));
}

void grpc_mock_endpoint_put_read(grpc_endpoint* ep, grpc_slice slice) {
  grpc_event_engine::experimental::Slice s(slice);
  static_cast<grpc_event_engine::experimental::MockEndpoint*>(
      grpc_event_engine::experimental::grpc_get_wrapped_event_engine_endpoint(
          ep))
      ->TriggerReadEvent(std::move(s));
}

void grpc_mock_endpoint_finish_put_reads(grpc_endpoint* ep) {
  static_cast<grpc_event_engine::experimental::MockEndpoint*>(
      grpc_event_engine::experimental::grpc_get_wrapped_event_engine_endpoint(
          ep))
      ->NoMoreReads();
}
