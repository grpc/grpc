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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_DATA_RATE_POLICY_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_PING_DATA_RATE_POLICY_H

#include <iosfwd>
#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "absl/types/variant.h"
#include "src/core/lib/gprpp/time.h"

namespace grpc_core {

class Chttp2PingRatePolicy {
 public:
  explicit Chttp2PingRatePolicy(const ChannelArgs& args, bool is_client);

  static void SetDefaults(const ChannelArgs& args);

  struct SendGranted {
    bool operator==(const SendGranted&) const { return true; }
  };
  struct TooManyRecentPings {
    bool operator==(const TooManyRecentPings&) const { return true; }
  };
  struct TooSoon {
    Duration next_allowed_ping_interval;
    Timestamp last_ping;
    Duration wait;
    bool operator==(const TooSoon& other) const {
      return next_allowed_ping_interval == other.next_allowed_ping_interval &&
             last_ping == other.last_ping && wait == other.wait;
    }
  };
  using RequestSendPingResult =
      absl::variant<SendGranted, TooManyRecentPings, TooSoon>;

  RequestSendPingResult RequestSendPing(Duration next_allowed_ping_interval);
  void ResetPingsBeforeDataRequired();
  void ReceivedDataFrame();
  std::string GetDebugString() const;

  int TestOnlyMaxPingsWithoutData() const { return max_pings_without_data_; }

 private:
  const int max_pings_without_data_;
  // No pings allowed before receiving a header or data frame.
  int pings_before_data_required_ = 0;
  Timestamp last_ping_sent_time_ = Timestamp::InfPast();
};

std::ostream& operator<<(std::ostream& out,
                         const Chttp2PingRatePolicy::RequestSendPingResult& r);

}  // namespace grpc_core

#endif
