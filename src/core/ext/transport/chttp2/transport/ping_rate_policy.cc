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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/ping_rate_policy.h"

#include <algorithm>
#include <ostream>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/impl/channel_arg_names.h>

#include "src/core/lib/gprpp/match.h"

namespace grpc_core {

namespace {
int g_default_max_pings_without_data = 2;
const ChannelArgs::IntKey kMaxPingsWithoutDataKey =
    ChannelArgs::IntKey::Register(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA,
                                  ChannelArgs::KeyOptions{});
}  // namespace

Chttp2PingRatePolicy::Chttp2PingRatePolicy(const ChannelArgs& args,
                                           bool is_client)
    : max_pings_without_data_(
          is_client
              ? std::max(0, args.GetInt(kMaxPingsWithoutDataKey)
                                .value_or(g_default_max_pings_without_data))
              : 0) {}

void Chttp2PingRatePolicy::SetDefaults(const ChannelArgs& args) {
  g_default_max_pings_without_data =
      std::max(0, args.GetInt(kMaxPingsWithoutDataKey)
                      .value_or(g_default_max_pings_without_data));
}

Chttp2PingRatePolicy::RequestSendPingResult
Chttp2PingRatePolicy::RequestSendPing(Duration next_allowed_ping_interval) {
  if (max_pings_without_data_ != 0 && pings_before_data_required_ == 0) {
    return TooManyRecentPings{};
  }
  const Timestamp next_allowed_ping =
      last_ping_sent_time_ + next_allowed_ping_interval;
  const Timestamp now = Timestamp::Now();
  if (next_allowed_ping > now) {
    return TooSoon{next_allowed_ping_interval, last_ping_sent_time_,
                   next_allowed_ping - now};
  }
  last_ping_sent_time_ = now;
  if (pings_before_data_required_) --pings_before_data_required_;
  return SendGranted{};
}

void Chttp2PingRatePolicy::ReceivedDataFrame() {
  last_ping_sent_time_ = Timestamp::InfPast();
}

void Chttp2PingRatePolicy::ResetPingsBeforeDataRequired() {
  pings_before_data_required_ = max_pings_without_data_;
}

std::string Chttp2PingRatePolicy::GetDebugString() const {
  return absl::StrCat(
      "max_pings_without_data: ", max_pings_without_data_,
      ", pings_before_data_required: ", pings_before_data_required_,
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
