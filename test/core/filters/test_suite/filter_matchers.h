// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CORE_FILTERS_TEST_SUITE_FILTER_MATCHERS_H
#define GRPC_TEST_CORE_FILTERS_TEST_SUITE_FILTER_MATCHERS_H

#include <ostream>
#include <string>

#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "gmock/gmock.h"
#include "absl/strings/escaping.h"

// gmock matchers for asserting on the metadata and messages that flow through a
// FilterTestV3 call. Shared with the older FilterTest<Filter> harness's
// vocabulary so tests read the same in both.

// Metadata has a given key with a given value.
MATCHER_P2(HasMetadataKeyValue, key, value, "") {
  std::string temp;
  auto r = arg.GetStringValue(key, &temp);
  return r == value;
}

// Metadata lacks a given key entirely.
MATCHER_P(LacksMetadataKey, key, "") {
  std::string temp;
  return !arg.GetStringValue(key, &temp).has_value();
}

// A message carries a given set of flags.
MATCHER_P(HasMessageFlags, value, "") { return arg.flags() == value; }

// Metadata encodes a given absl::Status (grpc-status + grpc-message).
MATCHER_P(HasMetadataResult, absl_status, "") {
  auto status = arg.get(grpc_core::GrpcStatusMetadata());
  if (!status.has_value()) return false;
  if (static_cast<absl::StatusCode>(status.value()) != absl_status.code()) {
    return false;
  }
  auto* message = arg.get_pointer(grpc_core::GrpcMessageMetadata());
  if (message == nullptr) return absl_status.message().empty();
  return message->as_string_view() == absl_status.message();
}

// A message has a given payload.
MATCHER_P(HasMessagePayload, value, "") {
  return arg.payload()->JoinIntoString() == value;
}

namespace grpc_core {

inline std::ostream& operator<<(std::ostream& os,
                                const grpc_metadata_batch& md) {
  return os << md.DebugString();
}

inline std::ostream& operator<<(std::ostream& os, const Message& msg) {
  return os << "flags:" << msg.flags()
            << " payload:" << absl::CEscape(msg.payload()->JoinIntoString());
}

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_FILTERS_TEST_SUITE_FILTER_MATCHERS_H
