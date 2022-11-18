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

#include <map>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"

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

  struct Entry {
    enum class EventType {
      kUnkown = 0,
      kClientHeader,
      kServerHeader,
      kClientMessage,
      kServerMessage,
      kClientHalfClose,
      kServerTrailer,
      kCancel
    };

    enum class Logger { kUnkown = 0, kClient, kServer };

    struct Payload {
      std::map<std::string, std::string> metadata;
      absl::Duration duration;
      uint32_t status_code;
      std::string status_message;
      std::string status_details;
      uint32_t message_length;
      std::string message;
    };

    struct Address {
      enum class Type { kUnknown = 0, kIpv4, kIpv6, kUnix };
      Type type;
      std::string address;
      uint32_t ip_port;
    };

    uint64_t call_id;
    uint64_t sequence_id;
    EventType type;
    Logger logger;
    Payload payload;
    bool payload_truncated;
    Address peer;
    std::string authority;
    std::string service_name;
    std::string method_name;
  };

  virtual ~LoggingSink() = default;

  virtual Config FindMatch(bool is_client, absl::string_view path) = 0;

  virtual void LogEntry(Entry entry) = 0;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_EXT_FILTERS_LOGGING_LOGGING_SINK_H
