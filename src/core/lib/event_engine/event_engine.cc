// Copyright 2023 The gRPC Authors
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
#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "absl/strings/str_cat.h"

namespace grpc_event_engine {
namespace experimental {

const EventEngine::TaskHandle EventEngine::TaskHandle::kInvalid = {-1, -1};
const EventEngine::ConnectionHandle EventEngine::ConnectionHandle::kInvalid = {
    -1, -1};

namespace detail {
std::string FormatHandleString(uint64_t key1, uint64_t key2) {
  return absl::StrCat("{", absl::Hex(key1, absl::kZeroPad16), ",",
                      absl::Hex(key2, absl::kZeroPad16), "}");
}
}  // namespace detail

namespace {
template <typename T>
bool eq(const T& lhs, const T& rhs) {
  return lhs.keys[0] == rhs.keys[0] && lhs.keys[1] == rhs.keys[1];
}
template <typename T>
std::ostream& printout(std::ostream& out, const T& handle) {
  out << detail::FormatHandleString(handle.keys[0], handle.keys[1]);
  return out;
}
}  // namespace

bool operator==(const EventEngine::TaskHandle& lhs,
                const EventEngine::TaskHandle& rhs) {
  return eq(lhs, rhs);
}

bool operator!=(const EventEngine::TaskHandle& lhs,
                const EventEngine::TaskHandle& rhs) {
  return !eq(lhs, rhs);
}

std::ostream& operator<<(std::ostream& out,
                         const EventEngine::TaskHandle& handle) {
  return printout(out, handle);
}

bool operator==(const EventEngine::ConnectionHandle& lhs,
                const EventEngine::ConnectionHandle& rhs) {
  return eq(lhs, rhs);
}

bool operator!=(const EventEngine::ConnectionHandle& lhs,
                const EventEngine::ConnectionHandle& rhs) {
  return !eq(lhs, rhs);
}

std::ostream& operator<<(std::ostream& out,
                         const EventEngine::ConnectionHandle& handle) {
  return printout(out, handle);
}

}  // namespace experimental
}  // namespace grpc_event_engine
