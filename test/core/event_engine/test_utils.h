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

#ifndef GRPC_TEST_CORE_EVENT_ENGINE_TEST_UTILS_H
#define GRPC_TEST_CORE_EVENT_ENGINE_TEST_UTILS_H

#include <grpc/support/port_platform.h>

#include <string>

#include "absl/strings/string_view.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice_buffer.h>

#include "src/core/lib/gprpp/notification.h"

namespace grpc_event_engine {
namespace experimental {

void AppendStringToSliceBuffer(SliceBuffer* buf, absl::string_view data);

std::string ExtractSliceBufferIntoString(SliceBuffer* buf);

EventEngine::ResolvedAddress URIToResolvedAddress(
    absl::string_view address_str);

// Returns a random message with bounded length.
std::string GetNextSendMessage();

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
  }
  ~NotifyOnDelete() {
    if (signal_ != nullptr) {
      signal_->Notify();
    }
  }

 private:
  grpc_core::Notification* signal_;
};

}  // namespace experimental
}  // namespace grpc_event_engine
#endif  // GRPC_TEST_CORE_EVENT_ENGINE_TEST_UTILS_H
