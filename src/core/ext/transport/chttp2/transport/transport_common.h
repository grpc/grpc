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

#include "src/core/lib/channel/channel_args.h"
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

#define KEEPALIVE_TIME_BACKOFF_MULTIPLIER 2

#define GRPC_CHTTP2_PING_TIMEOUT_STR "ping timeout"
#define GRPC_CHTTP2_KEEPALIVE_TIMEOUT_STR "keepalive timeout"

namespace grpc_core {

Duration TarpitDuration(int min_tarpit_duration_ms, int max_tarpit_duration_ms);

// Default keepalive settings for HTTP/2 transports, used when the corresponding
// keepalive channel args (GRPC_ARG_KEEPALIVE_TIME_MS,
// GRPC_ARG_KEEPALIVE_TIMEOUT_MS, GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS) are
// not set on a channel. Separate defaults are maintained for client and server
// transports. See https://github.com/grpc/grpc/blob/master/doc/keepalive.md for
// more details.
struct Http2KeepaliveDefaults {
  Duration keepalive_time;
  Duration keepalive_timeout;
  bool keepalive_permit_without_calls = false;
};

Http2KeepaliveDefaults GetHttp2KeepaliveDefaults(bool is_client);

// Overwrites the default keepalive settings for client or server transports.
// For use in tests to restore saved defaults.
void TestOnlySetHttp2KeepaliveDefaults(const Http2KeepaliveDefaults& defaults,
                                       bool is_client);

// Updates the default keepalive settings for client or server transports from
// `new_default_args`. Only the keepalive args present in `new_default_args` are
// updated; any unset arg keeps its current default value. Must be called from
// InitGRPC.
// Note: Despite the `ChannelArgs` type, `new_default_args` is not bound to a
// specific channel; its values set the process-wide gRPC defaults.
void SetHttp2KeepaliveDefaultsFromChannelArgs(
    const ChannelArgs& new_default_args, bool is_client);

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
