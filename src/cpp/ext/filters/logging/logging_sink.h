//
//
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
//
//

#ifndef GRPC_INTERNAL_CPP_EXT_FILTERS_LOGGING_LOGGING_SINK_H
#define GRPC_INTERNAL_CPP_EXT_FILTERS_LOGGING_LOGGING_SINK_H

#include <grpc/support/port_platform.h>

#include <stdint.h>

#include "absl/strings/string_view.h"

namespace grpc {
namespace internal {

// Interface for a logging sink that will be used by the logging filter.
class LoggingSink {
 public:
  class Config {
   public:
    Config(uint32_t max_metadata_bytes, uint32_t max_message_bytes)
        : max_metadata_bytes_(max_metadata_bytes),
          max_message_bytes_(max_message_bytes) {}
    bool MetadataLoggingEnabled() { return max_metadata_bytes_ != 0; }
    bool MessageLoggingEnabled() { return max_message_bytes_ != 0; }
    bool ShouldLog() {
      return MetadataLoggingEnabled() || MessageLoggingEnabled();
    }

    bool operator==(const Config& other) const {
      return max_metadata_bytes_ == other.max_metadata_bytes_ &&
             max_message_bytes_ == other.max_message_bytes_;
    }

   private:
    uint32_t max_metadata_bytes_;
    uint32_t max_message_bytes_;
  };

  virtual ~LoggingSink() = default;

  virtual Config FindMatch(bool is_client, absl::string_view path) = 0;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_FILTERS_LOGGING_LOGGING_SINK_H
