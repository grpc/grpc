// Copyright 2023 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/ping_abuse_policy.h"

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

namespace grpc_core {

namespace {
Duration g_default_min_recv_ping_interval_without_data = Duration::Minutes(5);
int g_default_max_ping_strikes = 2;
}  // namespace

Chttp2PingAbusePolicy::Chttp2PingAbusePolicy(const ChannelArgs& args)
    : min_recv_ping_interval_without_data_(std::max(
          Duration::Zero(),
          args.GetDurationFromIntMillis(
                  GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)
              .value_or(g_default_min_recv_ping_interval_without_data))),
      max_ping_strikes_(
          std::max(0, args.GetInt(GRPC_ARG_HTTP2_MAX_PING_STRIKES)
                          .value_or(g_default_max_ping_strikes))) {}

void Chttp2PingAbusePolicy::SetDefaults(const ChannelArgs& args) {
  g_default_max_ping_strikes =
      std::max(0, args.GetInt(GRPC_ARG_HTTP2_MAX_PING_STRIKES)
                      .value_or(g_default_max_ping_strikes));
  g_default_min_recv_ping_interval_without_data =
      std::max(Duration::Zero(),
               args.GetDurationFromIntMillis(
                       GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS)
                   .value_or(g_default_min_recv_ping_interval_without_data));
}

bool Chttp2PingAbusePolicy::ReceivedOnePing(bool transport_idle) {
  const Timestamp now = Timestamp::Now();
  const Timestamp next_allowed_ping =
      last_ping_recv_time_ + RecvPingIntervalWithoutData(transport_idle);
  last_ping_recv_time_ = now;
  if (next_allowed_ping <= now) return false;
  // Received ping too soon: increment strike count.
  ++ping_strikes_;
  return ping_strikes_ > max_ping_strikes_ && max_ping_strikes_ != 0;
}

std::string Chttp2PingAbusePolicy::GetDebugString(bool transport_idle) const {
  return absl::StrCat(
      "now=", Timestamp::Now().ToString(), " transport_idle=", transport_idle,
      " next_allowed_ping=",
      (last_ping_recv_time_ + RecvPingIntervalWithoutData(transport_idle))
          .ToString(),
      " ping_strikes=", ping_strikes_);
}

Duration Chttp2PingAbusePolicy::RecvPingIntervalWithoutData(
    bool transport_idle) const {
  if (transport_idle) {
    // According to RFC1122, the interval of TCP Keep-Alive is default to
    // no less than two hours. When there is no outstanding streams, we
    // restrict the number of PINGS equivalent to TCP Keep-Alive.
    return Duration::Hours(2);
  }
  return min_recv_ping_interval_without_data_;
}

void Chttp2PingAbusePolicy::ResetPingStrikes() {
  last_ping_recv_time_ = Timestamp::InfPast();
  ping_strikes_ = 0;
}

}  // namespace grpc_core
