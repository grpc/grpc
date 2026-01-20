//
//
// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_TRANSPORT_COMMON_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_TRANSPORT_COMMON_H

#include <cstdint>

#include "src/core/util/time.h"

// For an HTTP2 connection, this must be sent before the settings frame is sent.
#define GRPC_CHTTP2_CLIENT_CONNECT_STRING "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define GRPC_CHTTP2_CLIENT_CONNECT_STRLEN \
  (sizeof(GRPC_CHTTP2_CLIENT_CONNECT_STRING) - 1)

// EXPERIMENTAL: control tarpitting in chttp2
#define GRPC_ARG_HTTP_ALLOW_TARPIT "grpc.http.tarpit"
#define GRPC_ARG_HTTP_TARPIT_MIN_DURATION_MS "grpc.http.tarpit_min_duration_ms"
#define GRPC_ARG_HTTP_TARPIT_MAX_DURATION_MS "grpc.http.tarpit_max_duration_ms"

// EXPERIMENTAL: provide protection against overloading a server with too many
// requests: wait for streams to be deallocated before they stop counting
// against MAX_CONCURRENT_STREAMS
#define GRPC_ARG_MAX_CONCURRENT_STREAMS_OVERLOAD_PROTECTION \
  "grpc.http.overload_protection"

// EXPERIMENTAL: Fail requests at the client if the client is over max
// concurrent streams, so they may be retried elsewhere.
#define GRPC_ARG_MAX_CONCURRENT_STREAMS_REJECT_ON_CLIENT \
  "grpc.http.max_concurrent_streams_reject_on_client"

#define KEEPALIVE_TIME_BACKOFF_MULTIPLIER 2

#define GRPC_CHTTP2_PING_TIMEOUT_STR "ping timeout"
#define GRPC_CHTTP2_KEEPALIVE_TIMEOUT_STR "keepalive timeout"

namespace grpc_core {

Duration TarpitDuration(int min_tarpit_duration_ms, int max_tarpit_duration_ms);

namespace http2 {
enum class WritableStreamPriority : uint8_t {
  // Highest priority
  kStreamClosed = 0,
  kWaitForTransportFlowControl,
  // Lowest Priority
  kDefault,
  kLastPriority
};

// Debug helper function to convert a WritableStreamPriority to a string.
inline std::string GetWritableStreamPriorityString(
    const WritableStreamPriority priority) {
  switch (priority) {
    case WritableStreamPriority::kStreamClosed:
      return "StreamClosed";
    case WritableStreamPriority::kWaitForTransportFlowControl:
      return "WaitForTransportFlowControl";
    case WritableStreamPriority::kDefault:
      return "Default";
    default:
      return "unknown";
  }
}
}  // namespace http2

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_TRANSPORT_COMMON_H
