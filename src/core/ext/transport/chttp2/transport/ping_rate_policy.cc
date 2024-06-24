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

#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"

#include <algorithm>
#include <ostream>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/match.h"

// How many pings do we allow to be inflight at any given time?
// In older versions of gRPC this was implicitly 1.
// With the multiping experiment we allow this to rise to 100 by default.
// TODO(ctiller): consider making this public API
#define GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS "grpc.http2.max_inflight_pings"

namespace grpc_core {

namespace {
int g_default_max_pings_without_data_sent = 2;
constexpr Duration kThrottleIntervalWithoutDataSent = Duration::Minutes(1);
absl::optional<int> g_default_max_inflight_pings;
}  // namespace

Chttp2PingRatePolicy::Chttp2PingRatePolicy(const ChannelArgs& args,
                                           bool is_client)
    : max_pings_without_data_sent_(
          is_client
              ? std::max(0,
                         args.GetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA)
                             .value_or(g_default_max_pings_without_data_sent))
              : 0),
      // Configuration via channel arg dominates, otherwise if the multiping
      // experiment is enabled we use 100, otherwise 1.
      max_inflight_pings_(
          std::max(0, args.GetInt(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS)
                          .value_or(g_default_max_inflight_pings.value_or(
                              IsMultipingEnabled() ? 100 : 1)))) {}

void Chttp2PingRatePolicy::SetDefaults(const ChannelArgs& args) {
  g_default_max_pings_without_data_sent =
      std::max(0, args.GetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA)
                      .value_or(g_default_max_pings_without_data_sent));
  g_default_max_inflight_pings = args.GetInt(GRPC_ARG_HTTP2_MAX_INFLIGHT_PINGS);
}

Chttp2PingRatePolicy::RequestSendPingResult
Chttp2PingRatePolicy::RequestSendPing(Duration next_allowed_ping_interval,
                                      size_t inflight_pings) const {
  if (max_inflight_pings_ > 0 &&
      inflight_pings > static_cast<size_t>(max_inflight_pings_)) {
    return TooManyRecentPings{};
  }
  const Timestamp next_allowed_ping =
      last_ping_sent_time_ + next_allowed_ping_interval;
  const Timestamp now = Timestamp::Now();
  if (next_allowed_ping > now) {
    return TooSoon{next_allowed_ping_interval, last_ping_sent_time_,
                   next_allowed_ping - now};
  }
  // Throttle pings to 1 minute if we haven't sent any data recently
  if (max_pings_without_data_sent_ != 0 &&
      pings_before_data_sending_required_ == 0) {
    if (IsMaxPingsWoDataThrottleEnabled()) {
      const Timestamp next_allowed_ping =
          last_ping_sent_time_ + kThrottleIntervalWithoutDataSent;
      if (next_allowed_ping > now) {
        return TooSoon{kThrottleIntervalWithoutDataSent, last_ping_sent_time_,
                       next_allowed_ping - now};
      }
    } else {
      return TooManyRecentPings{};
    }
  }

  return SendGranted{};
}

void Chttp2PingRatePolicy::SentPing() {
  last_ping_sent_time_ = Timestamp::Now();
  if (pings_before_data_sending_required_ > 0) {
    --pings_before_data_sending_required_;
  }
}

void Chttp2PingRatePolicy::ReceivedDataFrame() {
  last_ping_sent_time_ = Timestamp::InfPast();
}

void Chttp2PingRatePolicy::ResetPingsBeforeDataRequired() {
  pings_before_data_sending_required_ = max_pings_without_data_sent_;
}

std::string Chttp2PingRatePolicy::GetDebugString() const {
  return absl::StrCat(
      "max_pings_without_data: ", max_pings_without_data_sent_,
      ", pings_before_data_required: ", pings_before_data_sending_required_,
      ", last_ping_sent_time_: ", last_ping_sent_time_.ToString());
}

std::ostream& operator<<(std::ostream& out,
                         const Chttp2PingRatePolicy::RequestSendPingResult& r) {
  Match(
      r, [&out](Chttp2PingRatePolicy::SendGranted) { out << "SendGranted"; },
      [&out](Chttp2PingRatePolicy::TooManyRecentPings) {
        out << "TooManyRecentPings";
      },
      [&out](Chttp2PingRatePolicy::TooSoon r) {
        out << "TooSoon: next_allowed="
            << r.next_allowed_ping_interval.ToString()
            << " last_ping_sent_time=" << r.last_ping.ToString()
            << " wait=" << r.wait.ToString();
      });
  return out;
}

}  // namespace grpc_core
