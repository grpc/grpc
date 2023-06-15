// Copyright 2022 gRPC authors.
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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_EVENT_ENGINE_TEST_UTILS_H
#define GRPC_TEST_CORE_EVENT_ENGINE_EVENT_ENGINE_TEST_UTILS_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/resource_quota/memory_quota.h"

using EventEngineFactory = std::function<
    std::unique_ptr<grpc_event_engine::experimental::EventEngine>()>;

namespace grpc_event_engine {
namespace experimental {

std::string ExtractSliceBufferIntoString(SliceBuffer* buf);

// Returns a random alphanumeric message with a random (bounded) length using
// predefined defaults.
std::string GetNextSendMessage();

// Returns a random alphanumeric message with a random (bounded) length.
std::string GetRandomBoundedMessage(size_t min_length, size_t max_length);

// Returns a random alphanumeric message with the provided message length.
std::string GetRandomMessage(size_t message_length);

// Waits until the use_count of the EventEngine shared_ptr has reached 1
// and returns.
// Callers must give up their ref, or this method will block forever.
// Usage: WaitForSingleOwner(std::move(engine))
void WaitForSingleOwner(std::shared_ptr<EventEngine> engine);

// A helper method to exchange data between two endpoints. It is assumed
// that both endpoints are connected. The data (specified as a string) is
// written by the sender_endpoint and read by the receiver_endpoint. It
// returns OK status only if data written == data read. It also blocks the
// calling thread until said Write and Read operations are complete.
absl::Status SendValidatePayload(absl::string_view data,
                                 EventEngine::Endpoint* send_endpoint,
                                 EventEngine::Endpoint* receive_endpoint);

void AppendStringToSliceBuffer(SliceBuffer* buf, absl::string_view data);

class NotifyOnDelete {
 public:
  explicit NotifyOnDelete(grpc_core::Notification* signal) : signal_(signal) {}
  NotifyOnDelete(const NotifyOnDelete&) = delete;
  NotifyOnDelete& operator=(const NotifyOnDelete&) = delete;
  NotifyOnDelete(NotifyOnDelete&& other) noexcept {
    signal_ = other.signal_;
    other.signal_ = nullptr;
  }
  NotifyOnDelete& operator=(NotifyOnDelete&& other) noexcept {
    signal_ = other.signal_;
    other.signal_ = nullptr;
    return *this;
  }
  ~NotifyOnDelete() {
    if (signal_ != nullptr) {
      signal_->Notify();
    }
  }

 private:
  grpc_core::Notification* signal_;
};

class SimpleConnectionFactory {
 public:
  struct Endpoints {
    std::unique_ptr<EventEngine::Endpoint> client;
    std::unique_ptr<EventEngine::Endpoint> listener;
  };

  // Create a simple connected pair of endpoints at the target address.
  static absl::StatusOr<Endpoints> Connect(EventEngine* client_engine,
                                           EventEngine* listener_engine,
                                           absl::string_view target_addr);
};

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GRPC_TEST_CORE_EVENT_ENGINE_EVENT_ENGINE_TEST_UTILS_H
