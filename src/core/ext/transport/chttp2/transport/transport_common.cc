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

#include "src/core/ext/transport/chttp2/transport/transport_common.h"

#include <grpc/impl/channel_arg_names.h>

#include <algorithm>
#include <cstdint>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/shared_bit_gen.h"
#include "src/core/util/time.h"
#include "absl/random/random.h"

namespace grpc_core {

namespace {

Http2KeepaliveDefaults g_client_keepalive_defaults = {
    /*keepalive_time=*/Duration::Infinity(),
    /*keepalive_timeout=*/Duration::Seconds(20),
    /*keepalive_permit_without_calls=*/false};

Http2KeepaliveDefaults g_server_keepalive_defaults = {
    /*keepalive_time=*/Duration::Hours(2),
    /*keepalive_timeout=*/Duration::Seconds(20),
    /*keepalive_permit_without_calls=*/false};

Http2KeepaliveDefaults& MutableHttp2KeepaliveDefaults(const bool is_client) {
  return is_client ? g_client_keepalive_defaults : g_server_keepalive_defaults;
}

}  // namespace

Duration TarpitDuration(const int min_tarpit_duration_ms,
                        const int max_tarpit_duration_ms) {
  if (min_tarpit_duration_ms > max_tarpit_duration_ms) {
    return Duration::Milliseconds(min_tarpit_duration_ms);
  }
  return Duration::Milliseconds(absl::LogUniform<int>(
      SharedBitGen(), min_tarpit_duration_ms, max_tarpit_duration_ms));
}

Http2KeepaliveDefaults GetHttp2KeepaliveDefaults(const bool is_client) {
  return MutableHttp2KeepaliveDefaults(is_client);
}

void TestOnlySetHttp2KeepaliveDefaults(const Http2KeepaliveDefaults& defaults,
                                       const bool is_client) {
  MutableHttp2KeepaliveDefaults(is_client) = defaults;
}

void SetHttp2KeepaliveDefaultsFromChannelArgs(
    const ChannelArgs& new_default_args, const bool is_client) {
  Http2KeepaliveDefaults& defaults = MutableHttp2KeepaliveDefaults(is_client);
  defaults.keepalive_time = std::max(
      Duration::Milliseconds(1),
      new_default_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIME_MS)
          .value_or(defaults.keepalive_time));
  defaults.keepalive_timeout = std::max(
      Duration::Zero(),
      new_default_args.GetDurationFromIntMillis(GRPC_ARG_KEEPALIVE_TIMEOUT_MS)
          .value_or(defaults.keepalive_timeout));
  defaults.keepalive_permit_without_calls =
      new_default_args.GetBool(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS)
          .value_or(defaults.keepalive_permit_without_calls);
}

}  // namespace grpc_core
