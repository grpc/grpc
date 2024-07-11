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
void grpc_mock_endpoint_put_read(grpc_endpoint* ep, grpc_slice slice);
void grpc_mock_endpoint_finish_put_reads(grpc_endpoint* ep);

namespace grpc_event_engine {
namespace experimental {

class MockEndpoint : public EventEngine::Endpoint {
 public:
  explicit MockEndpoint(std::shared_ptr<EventEngine> engine);

  ~MockEndpoint() override;

  // ---- mock methods ----
  void TriggerReadEvent(Slice read_data);
  void NoMoreReads();

  // ---- overrides ----
  bool Read(absl::AnyInvocable<void(absl::Status)> on_read, SliceBuffer* buffer,
            const ReadArgs* args) override;
  bool Write(absl::AnyInvocable<void(absl::Status)> on_writable,
             SliceBuffer* data, const WriteArgs* args) override;
  const EventEngine::ResolvedAddress& GetPeerAddress() const override;
  const EventEngine::ResolvedAddress& GetLocalAddress() const override;

 private:
  std::shared_ptr<EventEngine> engine_;
  grpc_core::Mutex mu_;
  SliceBuffer read_buffer_ ABSL_GUARDED_BY(mu_);
  bool reads_done_ ABSL_GUARDED_BY(mu_) = false;
  absl::AnyInvocable<void(absl::Status)> on_read_ ABSL_GUARDED_BY(mu_);
  SliceBuffer* on_read_slice_buffer_ ABSL_GUARDED_BY(mu_) = nullptr;
  EventEngine::ResolvedAddress peer_addr_;
  EventEngine::ResolvedAddress local_addr_;
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_TEST_UTIL_MOCK_ENDPOINT_H
