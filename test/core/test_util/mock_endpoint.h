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

#ifndef GRPC_TEST_CORE_TEST_UTIL_MOCK_ENDPOINT_H
#define GRPC_TEST_CORE_TEST_UTIL_MOCK_ENDPOINT_H

#include <memory>

#include <grpc/event_engine/event_engine.h>
#include <grpc/slice.h>

#include "src/core/lib/iomgr/endpoint.h"

grpc_endpoint* grpc_mock_endpoint_create(
    std::shared_ptr<grpc_event_engine::experimental::EventEngine> engine);

namespace grpc_event_engine {
namespace experimental {

// Internal controller object for mock endpoint operations.
//
// This helps avoid shared ownership issus. The endpoint itself may destroyed
// while a fuzzer is still attempting to use it (e.g., the transport is closed,
// and a fuzzer still wants to schedule reads).
class MockEndpointControl {
 public:
  explicit MockEndpointControl(std::shared_ptr<EventEngine> engine)
      : engine_(std::move(engine)) {}
  ~MockEndpointControl();

  // ---- mock methods ----
  void TriggerReadEvent(Slice read_data);
  void NoMoreReads();
  void Read(absl::AnyInvocable<void(absl::Status)> on_read,
            SliceBuffer* buffer);

  // ---- accessors ----
  EventEngine* engine() { return engine_.get(); }

 private:
  std::shared_ptr<EventEngine> engine_;
  grpc_core::Mutex mu_;
  bool reads_done_ ABSL_GUARDED_BY(mu_) = false;
  SliceBuffer read_buffer_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(absl::Status)> on_read_ ABSL_GUARDED_BY(mu_);
  SliceBuffer* on_read_slice_buffer_ ABSL_GUARDED_BY(mu_) = nullptr;
};

class MockEndpoint : public EventEngine::Endpoint {
 public:
  explicit MockEndpoint(std::shared_ptr<EventEngine> engine);
  ~MockEndpoint() override = default;

  // ---- mock methods ----
  // Get a reffed-added MockEndpointControl object.
  std::shared_ptr<MockEndpointControl> endpoint_control() {
    return endpoint_control_;
  }

  // ---- overrides ----
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override;
  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override;
  const EventEngine::ResolvedAddress& GetPeerAddress() const override;
  const EventEngine::ResolvedAddress& GetLocalAddress() const override;

 private:
  std::shared_ptr<MockEndpointControl> endpoint_control_;
  EventEngine::ResolvedAddress peer_addr_;
  EventEngine::ResolvedAddress local_addr_;
};

std::shared_ptr<MockEndpointControl> grpc_mock_endpoint_get_control(
    grpc_endpoint* ep);

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_TEST_UTIL_MOCK_ENDPOINT_H
