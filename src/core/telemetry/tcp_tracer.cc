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

#include "src/core/telemetry/tcp_tracer.h"

#include "absl/strings/str_join.h"

namespace grpc_core {

std::string TcpConnectionMetrics::ToString() {
  std::vector<std::string> parts;
  auto append_optional_string = [&](const std::string& name,
                                    const std::string& value) {
    if (!value.empty()) {
      parts.push_back(absl::StrCat(name, "=\'", value, "\'"));
    }
  };
  auto append_optional_bool = [&](const std::string& name,
                                  const std::optional<bool>& value) {
    if (value.has_value()) {
      parts.push_back(
          absl::StrCat(name, "=", value.value() ? "true" : "false", ""));
    }
  };
  auto append_optional = [&](const std::string& name, const auto& value) {
    if (value.has_value()) {
      parts.push_back(absl::StrCat(name, "=", value.value()));
    }
  };
  append_optional_string("congestion_ctrl", congestion_ctrl);
  append_optional("delivery_rate", delivery_rate);
  append_optional("data_retx", data_retx);
  append_optional("data_sent", data_sent);
  append_optional("packet_retx", packet_retx);
  append_optional("packet_spurious_retx", packet_spurious_retx);
  append_optional("packet_sent", packet_sent);
  append_optional("packet_delivered", packet_delivered);
  append_optional("packet_delivered_ce", packet_delivered_ce);
  append_optional("data_notsent", data_notsent);
  append_optional("min_rtt", min_rtt);
  append_optional("srtt", srtt);
  append_optional("ttl", ttl);
  append_optional("recurring_retrans", recurring_retrans);
  append_optional("net_rtt_usec", net_rtt_usec);
  append_optional("timeout_rehash", timeout_rehash);
  append_optional("ecn_rehash", ecn_rehash);
  append_optional("edt", edt);
  append_optional_bool("is_delivery_rate_app_limited",
                       is_delivery_rate_app_limited);
  append_optional("pacing_rate", pacing_rate);
  append_optional("congestion_window", congestion_window);
  append_optional("reordering", reordering);
  append_optional("busy_usec", busy_usec);
  append_optional("rwnd_limited_usec", rwnd_limited_usec);
  append_optional("sndbuf_limited_usec", sndbuf_limited_usec);
  append_optional("snd_ssthresh", snd_ssthresh);
  append_optional("time_to_ack_usec", time_to_ack_usec);
  append_optional("socket_errno", socket_errno);
  append_optional("peer_rwnd", peer_rwnd);
  append_optional("rcvq_drops", rcvq_drops);
  append_optional("nic_rx_delay_usec", nic_rx_delay_usec);
  return absl::StrJoin(parts, " ");
}

absl::string_view TcpCallTracer::TypeToString(Type type) {
  switch (type) {
    case Type::kSendMsg:
      return "SENDMSG";
    case Type::kScheduled:
      return "SCHEDULED";
    case Type::kSent:
      return "SENT";
    case Type::kAcked:
      return "ACKED";
    case Type::kClosed:
      return "CLOSED";
    default:
      return "UNKNOWN";
  }
}

}  // namespace grpc_core